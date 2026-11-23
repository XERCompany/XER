// Implementation of ai/img2img_runner.h — the tiled image->image
// runner. The grid: tile starts land on `tile - overlap` steps with
// the last tile of each axis shifted to end exactly at the frame
// edge (full-size model feeds everywhere the frame allows). Blend:
// every output pixel accumulates tile_px * wq into a u16 fixed-point
// buffer (wq = round(weight*256)) plus the weight sum, then
// normalizes — a constant input field comes out bit-exact at every
// seam, which is the property the smoke asserts.
#include "ai/img2img_runner.h"

#include <algorithm>
#include <cmath>

namespace XER::VideoEditor::Ai {
namespace {

bool ParamsValid(int w, int h, const Img2ImgParams& p,
                 std::string* error_out) {
    if (w <= 0 || h <= 0) {
        if (error_out) *error_out = "empty frame";
        return false;
    }
    if (p.tile < 8 || p.overlap < 0 || p.overlap * 2 > p.tile ||
        p.scale < 1) {
        if (error_out) {
            *error_out =
                "bad tile params (need tile>=8, 0<=overlap<=tile/2, "
                "scale>=1)";
        }
        return false;
    }
    return true;
}

// One axis of the tile grid: start positions on `step` with the last
// tile shifted to end exactly at `extent`. A frame smaller than the
// tile collapses to a single covering tile.
std::vector<int> AxisStarts(int extent, int tile, int step) {
    std::vector<int> starts;
    if (extent <= tile) {
        starts.push_back(0);
        return starts;
    }
    for (int x = 0; x + tile < extent; x += step) {
        starts.push_back(x);
    }
    const int last = extent - tile;
    if (starts.empty() || starts.back() != last) {
        starts.push_back(last);
    }
    return starts;
}

// Smoothstep ramp on [0, fade): 0 at the shared edge -> 1 where the
// tile stands alone. `d` is the distance in px from that edge.
float Ramp(int d, int fade) {
    if (fade <= 0) return 1.0f;
    if (d <= 0) return 0.0f;
    if (d >= fade) return 1.0f;
    const float t = static_cast<float>(d) / static_cast<float>(fade);
    return t * t * (3.0f - 2.0f * t);
}

}  // namespace

std::vector<Img2ImgTile> ComputeImg2ImgTiles(
    int w, int h, const Img2ImgParams& params) {
    std::vector<Img2ImgTile> tiles;
    if (w <= 0 || h <= 0 || params.tile < 8 || params.overlap < 0 ||
        params.overlap * 2 > params.tile || params.scale < 1) {
        return tiles;
    }
    const int step = params.tile - params.overlap;
    const std::vector<int> xs = AxisStarts(w, params.tile, step);
    const std::vector<int> ys = AxisStarts(h, params.tile, step);
    tiles.reserve(xs.size() * ys.size());
    for (const int y : ys) {
        for (const int x : xs) {
            Img2ImgTile t;
            t.x = x;
            t.y = y;
            t.w = std::min(params.tile, w - x);
            t.h = std::min(params.tile, h - y);
            tiles.push_back(t);
        }
    }
    return tiles;
}

float Img2ImgTileWeight(int px, int py, int tile_x0, int tile_y0,
                        int tile_w, int tile_h, int fade_l,
                        int fade_t, int fade_r, int fade_b) {
    const float wx =
        Ramp(px - tile_x0, fade_l) *
        Ramp(tile_x0 + tile_w - 1 - px, fade_r);
    const float wy =
        Ramp(py - tile_y0, fade_t) *
        Ramp(tile_y0 + tile_h - 1 - py, fade_b);
    return wx * wy;
}

bool RunImageToImage(const std::uint8_t* bgra, int w, int h,
                     int stride, const Img2ImgParams& params,
                     const Img2ImgTileFn& process_tile,
                     const std::function<void(float)>& progress,
                     const std::function<bool()>& should_cancel,
                     std::vector<std::uint8_t>* out_bgra, int* out_w,
                     int* out_h, std::string* error_out) {
    if (out_bgra == nullptr) {
        if (error_out) *error_out = "null output frame";
        return false;
    }
    out_bgra->clear();
    if (bgra == nullptr || process_tile == nullptr) {
        if (error_out) *error_out = "null frame or tile processor";
        return false;
    }
    if (!ParamsValid(w, h, params, error_out)) return false;
    if (stride < w * 4) {
        if (error_out) *error_out = "frame stride under w*4";
        return false;
    }
    const std::vector<Img2ImgTile> tiles =
        ComputeImg2ImgTiles(w, h, params);
    if (tiles.empty()) {
        if (error_out) *error_out = "tile grid is empty";
        return false;
    }

    const int ow = w * params.scale;
    const int oh = h * params.scale;
    if (out_w != nullptr) *out_w = ow;
    if (out_h != nullptr) *out_h = oh;

    const std::size_t px_count =
        static_cast<std::size_t>(ow) * static_cast<std::size_t>(oh);
    // Exact integer accumulation: acc = sum(px*wq), wacc = sum(wq)
    // with wq = round(weight*256) in [0,256]. u32 lanes keep the
    // worst case provable — up to 4 corner tiles each rounding up
    // puts sum(wq) at 258, so channel sums stay under 255*258 and
    // the only rounding anywhere is the final divide.
    std::vector<std::uint32_t> acc(px_count * 4u, 0);
    std::vector<std::uint16_t> wacc(px_count, 0);
    std::vector<std::uint8_t> tile_out;

    for (std::size_t i = 0; i < tiles.size(); ++i) {
        if (should_cancel != nullptr && should_cancel()) {
            if (error_out) *error_out = "cancelled";
            return false;
        }
        const Img2ImgTile& t = tiles[i];
        const int two = t.w * params.scale;
        const int toh = t.h * params.scale;
        tile_out.assign(static_cast<std::size_t>(two) * toh * 4u, 0);
        std::string err;
        const std::uint8_t* src =
            bgra + static_cast<std::size_t>(t.y) * stride +
            static_cast<std::size_t>(t.x) * 4u;
        if (!process_tile(src, t.w, t.h, stride, tile_out.data(), two,
                          toh, two * 4, &err)) {
            if (error_out) {
                *error_out =
                    err.empty() ? "tile processor failed" : err;
            }
            return false;
        }

        // Shared-zone widths (output px): the overlap with the
        // previous/next tile in each direction, 0 at frame edges.
        // Immediate neighbors always overlap (step < tile) and at
        // most one tile shares each side — the overlap<=tile/2
        // invariant keeps a third tile from reaching the zone.
        int fade_l = 0, fade_r = 0, fade_t = 0, fade_b = 0;
        for (const Img2ImgTile& o : tiles) {
            if (o.y == t.y && o.x < t.x && o.x + o.w > t.x) {
                fade_l = (o.x + o.w - t.x) * params.scale;
            }
            if (o.y == t.y && o.x > t.x && t.x + t.w > o.x) {
                fade_r = (t.x + t.w - o.x) * params.scale;
            }
            if (o.x == t.x && o.y < t.y && o.y + o.h > t.y) {
                fade_t = (o.y + o.h - t.y) * params.scale;
            }
            if (o.x == t.x && o.y > t.y && t.y + t.h > o.y) {
                fade_b = (t.y + t.h - o.y) * params.scale;
            }
        }
        const int ox0 = t.x * params.scale;
        const int oy0 = t.y * params.scale;
        for (int ty = 0; ty < toh; ++ty) {
            const int gy = oy0 + ty;
            std::uint32_t* acc_row =
                acc.data() + static_cast<std::size_t>(gy) * ow * 4u;
            std::uint16_t* wacc_row =
                wacc.data() + static_cast<std::size_t>(gy) * ow;
            const std::uint8_t* trow =
                tile_out.data() + static_cast<std::size_t>(ty) * two * 4u;
            const float wy =
                Ramp(ty, fade_t) * Ramp(toh - 1 - ty, fade_b);
            for (int tx = 0; tx < two; ++tx) {
                const float wx =
                    Ramp(tx, fade_l) * Ramp(two - 1 - tx, fade_r);
                const int wq = static_cast<int>(
                    std::lround(256.0f * wx * wy));
                if (wq == 0) continue;
                const int gx = ox0 + tx;
                std::uint32_t* a = acc_row + gx * 4;
                const std::uint8_t* p = trow + tx * 4;
                a[0] += static_cast<std::uint32_t>(p[0]) * wq;
                a[1] += static_cast<std::uint32_t>(p[1]) * wq;
                a[2] += static_cast<std::uint32_t>(p[2]) * wq;
                a[3] += static_cast<std::uint32_t>(p[3]) * wq;
                wacc_row[gx] = static_cast<std::uint16_t>(
                    wacc_row[gx] + wq);
            }
        }
        if (progress != nullptr) {
            progress(static_cast<float>(i + 1) /
                     static_cast<float>(tiles.size()));
        }
    }

    out_bgra->assign(px_count * 4u, 0);
    for (std::size_t p = 0; p < px_count; ++p) {
        const std::uint32_t wsum = wacc[p];
        if (wsum == 0) continue;   // unreachable on a covering grid
        std::uint8_t* dst = out_bgra->data() + p * 4u;
        const std::uint32_t* a = acc.data() + p * 4u;
        dst[0] = static_cast<std::uint8_t>(
            (a[0] + wsum / 2) / wsum);
        dst[1] = static_cast<std::uint8_t>(
            (a[1] + wsum / 2) / wsum);
        dst[2] = static_cast<std::uint8_t>(
            (a[2] + wsum / 2) / wsum);
        dst[3] = static_cast<std::uint8_t>(
            (a[3] + wsum / 2) / wsum);
    }
    return true;
}

}  // namespace XER::VideoEditor::Ai
