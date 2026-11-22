// See object_ops.h — pure-frame BGRA8 object operations for the Object
// Lab. Nothing here touches decoders, files or UI.
#include "object_ops.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include "mkl_accel.h"

namespace XER::VideoEditor::Lab {
namespace {

bool IsBgra(const video::Frame& frame) {
    return frame.IsValid() && frame.Format() == video::PixelFormat::BGRA8;
}

IntRect ClampRect(IntRect r, int w, int h) {
    const int x0 = std::clamp(r.x, 0, w);
    const int y0 = std::clamp(r.y, 0, h);
    const int x1 = std::clamp(r.x + r.w, 0, w);
    const int y1 = std::clamp(r.y + r.h, 0, h);
    return {x0, y0, std::max(0, x1 - x0), std::max(0, y1 - y0)};
}

// Scanline even-odd fill of `polygon` into a w*h 0/1 inside map,
// sampling at pixel centers.
std::vector<std::uint8_t> InsideMap(int w, int h,
                                    const std::vector<IntPoint>& poly) {
    std::vector<std::uint8_t> inside(static_cast<size_t>(w) * h, 0);
    std::vector<float> crossings;
    const size_t n = poly.size();
    for (int y = 0; y < h; ++y) {
        const float cy = static_cast<float>(y) + 0.5f;
        crossings.clear();
        for (size_t i = 0; i < n; ++i) {
            const IntPoint a = poly[i];
            const IntPoint b = poly[(i + 1) % n];
            if ((a.y <= cy) == (b.y <= cy)) continue;  // not straddling
            const float t = (cy - static_cast<float>(a.y)) /
                            static_cast<float>(b.y - a.y);
            crossings.push_back(static_cast<float>(a.x) +
                                t * static_cast<float>(b.x - a.x));
        }
        if (crossings.size() < 2) continue;
        std::sort(crossings.begin(), crossings.end());
        std::uint8_t* row = inside.data() + static_cast<size_t>(y) * w;
        for (size_t k = 0; k + 1 < crossings.size(); k += 2) {
            const int x0 = std::clamp(
                static_cast<int>(std::ceil(crossings[k] - 0.5f)), 0, w);
            const int x1 = std::clamp(
                static_cast<int>(std::ceil(crossings[k + 1] - 0.5f)), 0, w);
            for (int x = x0; x < x1; ++x) row[x] = 1;
        }
    }
    return inside;
}

float PointSegmentDistance(float px, float py, float ax, float ay,
                           float bx, float by) {
    const float ex = bx - ax;
    const float ey = by - ay;
    const float len2 = ex * ex + ey * ey;
    float t = 0.0f;
    if (len2 > 0.0f) {
        t = ((px - ax) * ex + (py - ay) * ey) / len2;
        t = std::clamp(t, 0.0f, 1.0f);
    }
    const float dx = px - (ax + t * ex);
    const float dy = py - (ay + t * ey);
    return std::sqrt(dx * dx + dy * dy);
}

// Min distance from (px,py) to the polygon outline. Segment bounding
// boxes give a cheap lower bound, so far segments are skipped after
// four compares instead of a full projection.
float DistanceToPolygon(float px, float py,
                        const std::vector<IntPoint>& poly) {
    float best = std::numeric_limits<float>::max();
    const size_t n = poly.size();
    for (size_t i = 0; i < n; ++i) {
        const IntPoint a = poly[i];
        const IntPoint b = poly[(i + 1) % n];
        const float lo_x = static_cast<float>(std::min(a.x, b.x));
        const float hi_x = static_cast<float>(std::max(a.x, b.x));
        const float lo_y = static_cast<float>(std::min(a.y, b.y));
        const float hi_y = static_cast<float>(std::max(a.y, b.y));
        const float dx = px < lo_x ? lo_x - px
                         : px > hi_x ? px - hi_x
                                     : 0.0f;
        const float dy = py < lo_y ? lo_y - py
                         : py > hi_y ? py - hi_y
                                     : 0.0f;
        if (dx * dx + dy * dy >= best * best) continue;
        const float d = PointSegmentDistance(
            px, py, static_cast<float>(a.x), static_cast<float>(a.y),
            static_cast<float>(b.x), static_cast<float>(b.y));
        best = std::min(best, d);
    }
    return best;
}

// Iterative Douglas-Peucker on a contour (open chain): returns the
// kept vertex subset in order. tolerance <= 0 keeps everything.
std::vector<IntPoint> SimplifyPolygon(
    const std::vector<IntPoint>& pts, float tolerance) {
    if (pts.size() <= 2 || tolerance <= 0.0f) return pts;
    std::vector<std::uint8_t> keep(pts.size(), 0);
    keep.front() = keep.back() = 1;
    std::vector<std::pair<size_t, size_t>> stack;
    stack.emplace_back(0, pts.size() - 1);
    while (!stack.empty()) {
        const auto [a, b] = stack.back();
        stack.pop_back();
        if (b <= a + 1) continue;
        float best = -1.0f;
        size_t best_i = a;
        for (size_t i = a + 1; i < b; ++i) {
            const float d = PointSegmentDistance(
                static_cast<float>(pts[i].x), static_cast<float>(pts[i].y),
                static_cast<float>(pts[a].x), static_cast<float>(pts[a].y),
                static_cast<float>(pts[b].x), static_cast<float>(pts[b].y));
            if (d > best) {
                best = d;
                best_i = i;
            }
        }
        if (best > tolerance) {
            keep[best_i] = 1;
            stack.emplace_back(a, best_i);
            stack.emplace_back(best_i, b);
        }
    }
    std::vector<IntPoint> out;
    out.reserve(pts.size());
    for (size_t i = 0; i < pts.size(); ++i) {
        if (keep[i]) out.push_back(pts[i]);
    }
    return out;
}

// Chamfer (1, ~sqrt2) distance to the mask boundary — used only to
// select the band of pixels that need an exact segment distance; the
// band threshold is generous so approximation error is harmless.
std::vector<float> BoundaryDistance(const std::vector<std::uint8_t>& inside,
                                    int w, int h) {
    const float inf = 1.0e9f;
    const float diag = 1.41421356f;
    std::vector<float> dist(inside.size(), inf);
    auto is_inside = [&](int x, int y) {
        return inside[static_cast<size_t>(y) * w + x] != 0;
    };
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const bool v = is_inside(x, y);
            const bool boundary =
                (x > 0 && is_inside(x - 1, y) != v) ||
                (x + 1 < w && is_inside(x + 1, y) != v) ||
                (y > 0 && is_inside(x, y - 1) != v) ||
                (y + 1 < h && is_inside(x, y + 1) != v);
            if (boundary) dist[static_cast<size_t>(y) * w + x] = 0.0f;
        }
    }
    auto at = [&](int x, int y) -> float& {
        return dist[static_cast<size_t>(y) * w + x];
    };
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float d = at(x, y);
            if (x > 0) d = std::min(d, at(x - 1, y) + 1.0f);
            if (y > 0) {
                d = std::min(d, at(x, y - 1) + 1.0f);
                if (x > 0) d = std::min(d, at(x - 1, y - 1) + diag);
                if (x + 1 < w) d = std::min(d, at(x + 1, y - 1) + diag);
            }
            at(x, y) = d;
        }
    }
    for (int y = h - 1; y >= 0; --y) {
        for (int x = w - 1; x >= 0; --x) {
            float d = at(x, y);
            if (x + 1 < w) d = std::min(d, at(x + 1, y) + 1.0f);
            if (y + 1 < h) {
                d = std::min(d, at(x, y + 1) + 1.0f);
                if (x + 1 < w) d = std::min(d, at(x + 1, y + 1) + diag);
                if (x > 0) d = std::min(d, at(x - 1, y + 1) + diag);
            }
            at(x, y) = d;
        }
    }
    return dist;
}

// ------------------------ inpainting ------------------------------

struct XorShift {
    std::uint32_t s;
    explicit XorShift(std::uint32_t seed) : s(seed == 0 ? 1u : seed) {}
    std::uint32_t Next() {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return s;
    }
    int Range(int n) { return n > 0 ? static_cast<int>(Next() % n) : 0; }
    int Sym(int r) { return Range(2 * r + 1) - r; }  // uniform in [-r,r]
};

bool Cancelled(const InpaintParams& p) {
    return p.should_cancel && p.should_cancel();
}

void Progress(const InpaintParams& p, float f) {
    if (p.report_progress) p.report_progress(std::clamp(f, 0.0f, 1.0f));
}

// Patch-match-lite exemplar inpainting.
//
// Approximation notes (deliberately lighter than full Criminisi /
// PatchMatch):
//  * The nearest-neighbor field is per hole PIXEL (p -> source offset)
//    and only source-side patches (fully outside the hole) are scored.
//  * Patch SSD counts only taps that are KNOWN on the target side —
//    i.e. original pixels plus hole pixels already filled earlier in
//    the same sweep — so the fill grows inward from the boundary like
//    an onion peel, in scan order, alternating direction each sweep.
//  * A candidate is rejected unless >= 1/4 of the patch taps matched,
//    and scores are normalized by matched-tap count so partial matches
//    stay comparable.
//  * After the sweeps, a light voting pass averages each hole pixel's
//    own suggestion with the offsets propagated by its filled
//    4-neighbors; stragglers that never matched get a diffusion fill.
//  * SSD itself runs through Lab::PatchSsd, i.e. MKL/OpenBLAS sdot when
//    a backend probed successfully.
bool InpaintImpl(video::Frame& frame,
                 const std::vector<std::uint8_t>& hole,
                 const InpaintParams& params) {
    const int w = frame.Width();
    const int h = frame.Height();
    const int pixel_count = w * h;

    std::vector<int> hole_idx(pixel_count, -1);
    std::vector<IntPoint> hole_pixels;
    hole_pixels.reserve(pixel_count / 8 + 1);
    int hole_count = 0;
    for (int i = 0; i < pixel_count; ++i) {
        if (hole[i] == 0) continue;
        hole_idx[i] = hole_count++;
        hole_pixels.push_back({i % w, i / w});
    }
    if (hole_count == 0) return true;          // nothing to do
    if (hole_count == pixel_count) return false;  // no source pixels

    // Float work buffer, BGR (alpha untouched until write-back).
    std::vector<float> buf(static_cast<size_t>(pixel_count) * 3);
    const std::uint8_t* src = frame.Data();
    for (int i = 0; i < pixel_count; ++i) {
        buf[3 * i + 0] = static_cast<float>(src[4 * i + 0]);
        buf[3 * i + 1] = static_cast<float>(src[4 * i + 1]);
        buf[3 * i + 2] = static_cast<float>(src[4 * i + 2]);
    }

    std::vector<std::uint8_t> known(hole.size());
    for (size_t i = 0; i < hole.size(); ++i) known[i] = !hole[i];
    const std::vector<std::uint8_t>& is_hole = hole;

    const int radius = std::clamp(params.search_radius, 1, 512);
    const int pr = std::clamp(params.patch_radius, 1, 8);
    const int patch_area = (2 * pr + 1) * (2 * pr + 1);
    const int min_taps = std::max(3, patch_area / 4);
    const int iterations = std::clamp(params.iterations, 1, 32);

    const int kInvalid = std::numeric_limits<int>::min();
    std::vector<IntPoint> offset(hole_count, {kInvalid, kInvalid});
    XorShift rng(params.seed ^
                 static_cast<std::uint32_t>(hole_count * 2654435761u));

    auto in_bounds = [w, h](int x, int y) {
        return x >= 0 && y >= 0 && x < w && y < h;
    };
    auto is_source = [&](int x, int y) {
        return in_bounds(x, y) && !is_hole[static_cast<size_t>(y) * w + x];
    };

    std::vector<float> taps_a, taps_b;
    taps_a.reserve(static_cast<size_t>(patch_area) * 3);
    taps_b.reserve(static_cast<size_t>(patch_area) * 3);
    const float kInf = std::numeric_limits<float>::max();

    // Normalized SSD of the (2pr+1)^2 patch around p vs the patch
    // around p+off; only mutually-valid taps count.
    auto evaluate = [&](const IntPoint& p, IntPoint off) -> float {
        if (off.x < -radius || off.x > radius || off.y < -radius ||
            off.y > radius) {
            return kInf;
        }
        const int qx = p.x + off.x;
        const int qy = p.y + off.y;
        if (!is_source(qx, qy)) return kInf;
        taps_a.clear();
        taps_b.clear();
        for (int dy = -pr; dy <= pr; ++dy) {
            const int ty = p.y + dy;
            const int sy = qy + dy;
            for (int dx = -pr; dx <= pr; ++dx) {
                const int tx = p.x + dx;
                const int sx = qx + dx;
                if (!in_bounds(tx, ty) || !in_bounds(sx, sy)) continue;
                const size_t ti = static_cast<size_t>(ty) * w + tx;
                const size_t si = static_cast<size_t>(sy) * w + sx;
                if (!known[ti] || is_hole[si]) continue;
                for (int c = 0; c < 3; ++c) {
                    taps_a.push_back(buf[3 * ti + c]);
                    taps_b.push_back(buf[3 * si + c]);
                }
            }
        }
        const int matched = static_cast<int>(taps_a.size()) / 3;
        if (matched < min_taps) return kInf;
        return PatchSsd(static_cast<int>(taps_a.size()), taps_a.data(),
                        taps_b.data()) /
               static_cast<float>(matched);
    };

    const int dx4[4] = {-1, 1, 0, 0};
    const int dy4[4] = {0, 0, -1, 1};

    Progress(params, 0.02f);
    for (int iter = 0; iter < iterations; ++iter) {
        if (Cancelled(params)) return false;
        const bool forward = (iter % 2) == 0;
        int since_check = 0;
        for (int step = 0; step < hole_count; ++step) {
            if (++since_check >= 512) {
                since_check = 0;
                if (Cancelled(params)) return false;
            }
            const int hi = forward ? step : hole_count - 1 - step;
            const IntPoint p = hole_pixels[hi];

            // Candidate offsets: current, propagated from filled
            // neighbours, and fresh random samples within the radius.
            IntPoint candidates[16];
            int cand_count = 0;
            if (offset[hi].x != kInvalid) {
                candidates[cand_count++] = offset[hi];
            }
            for (int k = 0; k < 4 && cand_count < 6; ++k) {
                const int nx = p.x + dx4[k];
                const int ny = p.y + dy4[k];
                if (!in_bounds(nx, ny)) continue;
                const int ni = hole_idx[static_cast<size_t>(ny) * w + nx];
                if (ni >= 0 && offset[ni].x != kInvalid) {
                    candidates[cand_count++] = offset[ni];
                }
            }
            const int random_n =
                std::clamp(params.random_candidates, 0, 8);
            for (int k = 0; k < random_n && cand_count < 16; ++k) {
                const IntPoint cand{rng.Sym(radius), rng.Sym(radius)};
                if (is_source(p.x + cand.x, p.y + cand.y)) {
                    candidates[cand_count++] = cand;
                }
            }

            float best_ssd = kInf;
            IntPoint best{kInvalid, kInvalid};
            for (int k = 0; k < cand_count; ++k) {
                const float ssd = evaluate(p, candidates[k]);
                if (ssd < best_ssd) {
                    best_ssd = ssd;
                    best = candidates[k];
                }
            }
            if (best.x == kInvalid) continue;  // stay unfilled this sweep
            offset[hi] = best;
            const size_t pi = static_cast<size_t>(p.y) * w + p.x;
            const size_t qi =
                static_cast<size_t>(p.y + best.y) * w + (p.x + best.x);
            for (int c = 0; c < 3; ++c) buf[3 * pi + c] = buf[3 * qi + c];
            known[pi] = 1;
        }
        Progress(params,
                 0.02f + 0.86f * static_cast<float>(iter + 1) /
                             static_cast<float>(iterations));
    }

    // Diffusion fallback for hole pixels that never matched (deep
    // interior vs a too-small search radius). Guaranteed to terminate:
    // the hole is bounded and at least one boundary pixel has a known
    // neighbour on every pass.
    for (int pass = 0; pass < pixel_count; ++pass) {
        bool filled_any = false;
        bool pending_any = false;
        for (int hi = 0; hi < hole_count; ++hi) {
            const IntPoint p = hole_pixels[hi];
            const size_t pi = static_cast<size_t>(p.y) * w + p.x;
            if (known[pi]) continue;
            float acc[3] = {0, 0, 0};
            int n = 0;
            for (int k = 0; k < 4; ++k) {
                const int nx = p.x + dx4[k];
                const int ny = p.y + dy4[k];
                if (!in_bounds(nx, ny)) continue;
                const size_t ni = static_cast<size_t>(ny) * w + nx;
                if (!known[ni]) continue;
                for (int c = 0; c < 3; ++c) acc[c] += buf[3 * ni + c];
                ++n;
            }
            if (n == 0) {
                pending_any = true;
                continue;
            }
            for (int c = 0; c < 3; ++c) buf[3 * pi + c] = acc[c] / n;
            known[pi] = 1;
            filled_any = true;
        }
        if (!pending_any) break;
        if (!filled_any) return false;  // enclosed hole w/o any source
        if (Cancelled(params)) return false;
    }

    // Voting pass: blend each hole pixel's own source suggestion with
    // its filled neighbours' offsets (coherence smoothing).
    for (int hi = 0; hi < hole_count; ++hi) {
        const IntPoint p = hole_pixels[hi];
        const size_t pi = static_cast<size_t>(p.y) * w + p.x;
        float acc[3] = {0, 0, 0};
        float weight = 0.0f;
        auto vote = [&](IntPoint off, float wgt) {
            const int qx = p.x + off.x;
            const int qy = p.y + off.y;
            if (!is_source(qx, qy)) return;
            const size_t qi = static_cast<size_t>(qy) * w + qx;
            for (int c = 0; c < 3; ++c) acc[c] += buf[3 * qi + c] * wgt;
            weight += wgt;
        };
        if (offset[hi].x != kInvalid) vote(offset[hi], 2.0f);
        for (int k = 0; k < 4; ++k) {
            const int nx = p.x + dx4[k];
            const int ny = p.y + dy4[k];
            if (!in_bounds(nx, ny)) continue;
            const int ni = hole_idx[static_cast<size_t>(ny) * w + nx];
            if (ni >= 0 && offset[ni].x != kInvalid) {
                vote(offset[ni], 1.0f);
            }
        }
        if (weight > 0.0f) {
            for (int c = 0; c < 3; ++c) buf[3 * pi + c] = acc[c] / weight;
        }
    }
    Progress(params, 0.95f);
    if (Cancelled(params)) return false;

    // Write back: hole pixels become opaque, neighbours untouched.
    std::uint8_t* dst = frame.Data();
    for (int hi = 0; hi < hole_count; ++hi) {
        const IntPoint p = hole_pixels[hi];
        const size_t pi = static_cast<size_t>(p.y) * w + p.x;
        for (int c = 0; c < 3; ++c) {
            dst[4 * pi + c] = static_cast<std::uint8_t>(
                std::clamp(std::lround(buf[3 * pi + c]), 0l, 255l));
        }
        dst[4 * pi + 3] = 255;
    }
    Progress(params, 1.0f);
    return true;
}

}  // namespace

// ------------------------------------------------------------------
// Masks
// ------------------------------------------------------------------

std::vector<std::uint8_t> RectMask(int frame_w, int frame_h,
                                   IntRect rect) {
    if (frame_w <= 0 || frame_h <= 0) return {};
    std::vector<std::uint8_t> mask(
        static_cast<size_t>(frame_w) * frame_h, 0);
    const IntRect r = ClampRect(rect, frame_w, frame_h);
    for (int y = r.y; y < r.y + r.h; ++y) {
        std::fill_n(mask.begin() + static_cast<size_t>(y) * frame_w + r.x,
                    r.w, static_cast<std::uint8_t>(255));
    }
    return mask;
}

std::vector<std::uint8_t> PolygonMask(
    int frame_w, int frame_h, const std::vector<IntPoint>& polygon,
    int feather_px) {
    if (frame_w <= 0 || frame_h <= 0 || polygon.size() < 3) return {};
    const size_t count = static_cast<size_t>(frame_w) * frame_h;
    const std::vector<std::uint8_t> inside =
        InsideMap(frame_w, frame_h, polygon);
    if (feather_px <= 0) {
        std::vector<std::uint8_t> mask(count);
        for (size_t i = 0; i < count; ++i) {
            mask[i] = inside[i] ? 255 : 0;
        }
        return mask;
    }

    const float feather = static_cast<float>(feather_px);
    const std::vector<float> band_dist =
        BoundaryDistance(inside, frame_w, frame_h);
    std::vector<std::uint8_t> mask(count);
    for (int y = 0; y < frame_h; ++y) {
        for (int x = 0; x < frame_w; ++x) {
            const size_t i = static_cast<size_t>(y) * frame_w + x;
            float coverage;
            if (band_dist[i] >= feather) {
                // Outside the feather band the ramp is saturated.
                coverage = inside[i] ? 1.0f : 0.0f;
            } else {
                const float d =
                    DistanceToPolygon(static_cast<float>(x) + 0.5f,
                                      static_cast<float>(y) + 0.5f,
                                      polygon);
                const float signed_d = inside[i] ? d : -d;
                coverage = std::clamp(0.5f + signed_d / feather,
                                      0.0f, 1.0f);
            }
            mask[i] = static_cast<std::uint8_t>(
                std::lround(coverage * 255.0f));
        }
    }
    return mask;
}

std::vector<IntPoint> MaskToPolygon(
    const std::vector<std::uint8_t>& mask, int frame_w, int frame_h,
    float tolerance_px) {
    if (frame_w <= 0 || frame_h <= 0 ||
        mask.size() < static_cast<size_t>(frame_w) * frame_h) {
        return {};
    }
    auto inside = [&](int x, int y) {
        return x >= 0 && x < frame_w && y >= 0 && y < frame_h &&
               mask[static_cast<size_t>(y) * frame_w + x] >= 128;
    };
    // First inside pixel in scan order anchors the outer contour —
    // its top edge is guaranteed to be a boundary edge (nothing
    // inside above it), which gives the walk a deterministic start.
    IntPoint start{-1, -1};
    for (int y = 0; y < frame_h && start.x < 0; ++y) {
        for (int x = 0; x < frame_w; ++x) {
            if (inside(x, y)) {
                start = {x, y};
                break;
            }
        }
    }
    if (start.x < 0) return {};

    // Crack-following boundary trace on the cell-corner lattice: walk
    // edges that keep inside pixels on the right. An edge leaving
    // corner (cx,cy) in direction d is a boundary edge iff the cell on
    // its inside side is set and the cell on its outside side is not.
    // At each corner prefer turning right (hug the inside), then
    // straight, then left, then the u-turn (isolated pixels).
    // Directions: 0=E, 1=S, 2=W, 3=N.
    static const int kDx[4] = {1, 0, -1, 0};
    static const int kDy[4] = {0, 1, 0, -1};
    // {inside-side cell, outside-side cell} relative to the corner an
    // edge departs from, per direction.
    static const IntPoint kIn[4] = {{0, 0}, {-1, 0}, {-1, -1}, {0, -1}};
    static const IntPoint kOut[4] = {{0, -1}, {0, 0}, {-1, 0}, {-1, -1}};
    auto edge_ok = [&](int cx, int cy, int d) {
        return inside(cx + kIn[d].x, cy + kIn[d].y) &&
               !inside(cx + kOut[d].x, cy + kOut[d].y);
    };

    std::vector<IntPoint> contour;
    int cx = start.x, cy = start.y, dir = 0;
    contour.push_back({cx, cy});
    // Boundary edges are visited once per direction — the walk is
    // finite; the cap guards against a malformed-mask hang anyway.
    const size_t cap =
        static_cast<size_t>(frame_w) * frame_h * 4 + 64;
    for (size_t guard = 0; guard < cap; ++guard) {
        const int order[4] = {(dir + 1) % 4, dir, (dir + 3) % 4,
                              (dir + 2) % 4};
        int nd = -1;
        for (int t = 0; t < 4 && nd < 0; ++t) {
            if (edge_ok(cx, cy, order[t])) nd = order[t];
        }
        if (nd < 0) break;
        // Closed when the walk is back at the start corner choosing
        // the start direction — the next edge would retrace the
        // first. The first pick is exempt: it IS the start corner
        // choosing the start direction by construction.
        if (contour.size() > 1 && cx == start.x && cy == start.y &&
            nd == 0) {
            break;
        }
        cx += kDx[nd];
        cy += kDy[nd];
        dir = nd;
        contour.push_back({cx, cy});
    }
    if (contour.size() < 3) return contour;

    std::vector<IntPoint> simplified =
        SimplifyPolygon(contour, tolerance_px);
    // Corner coords are the true mask edge; clamp into frame bounds
    // and keep at least a triangle so the polygon stays a valid
    // lasso/PolygonMask input.
    for (auto& pt : simplified) {
        pt.x = std::clamp(pt.x, 0, frame_w);
        pt.y = std::clamp(pt.y, 0, frame_h);
    }
    return simplified.size() >= 3 ? simplified : contour;
}

// ------------------------------------------------------------------
// Cut / copy
// ------------------------------------------------------------------

video::Frame CutOutRect(const video::Frame& frame, IntRect rect) {
    video::Frame out;
    if (!IsBgra(frame)) return out;
    const int w = frame.Width();
    const int h = frame.Height();
    const IntRect r = ClampRect(rect, w, h);
    if (r.w <= 0 || r.h <= 0) return out;
    out.Allocate(w, h, video::PixelFormat::BGRA8);
    const std::uint8_t* src = frame.Data();
    std::uint8_t* dst = out.Data();
    for (int y = r.y; y < r.y + r.h; ++y) {
        for (int x = r.x; x < r.x + r.w; ++x) {
            const size_t i = static_cast<size_t>(y) * w + x;
            dst[4 * i + 0] = src[4 * i + 0];
            dst[4 * i + 1] = src[4 * i + 1];
            dst[4 * i + 2] = src[4 * i + 2];
            dst[4 * i + 3] = 255;
        }
    }
    return out;
}

video::Frame CutOutLasso(const video::Frame& frame,
                         const std::vector<IntPoint>& polygon,
                         int feather_px) {
    if (!IsBgra(frame) || polygon.size() < 3) return {};
    const std::vector<std::uint8_t> mask = PolygonMask(
        frame.Width(), frame.Height(), polygon, feather_px);
    return CopyRegionToLayer(frame, mask);
}

video::Frame CopyRegionToLayer(
    const video::Frame& frame, const std::vector<std::uint8_t>& mask) {
    video::Frame out;
    if (!IsBgra(frame)) return out;
    const int w = frame.Width();
    const int h = frame.Height();
    if (mask.size() != static_cast<size_t>(w) * h) return out;
    out.Allocate(w, h, video::PixelFormat::BGRA8);
    const std::uint8_t* src = frame.Data();
    std::uint8_t* dst = out.Data();
    for (size_t i = 0; i < mask.size(); ++i) {
        const std::uint8_t m = mask[i];
        if (m == 0) continue;
        dst[4 * i + 0] = src[4 * i + 0];
        dst[4 * i + 1] = src[4 * i + 1];
        dst[4 * i + 2] = src[4 * i + 2];
        dst[4 * i + 3] = m;
    }
    return out;
}

// ------------------------------------------------------------------
// Inpainting
// ------------------------------------------------------------------

bool InpaintRect(video::Frame& frame, IntRect rect, int search_radius) {
    InpaintParams params;
    params.search_radius = search_radius;
    return InpaintRect(frame, rect, params);
}

bool InpaintRect(video::Frame& frame, IntRect rect,
                 const InpaintParams& params) {
    if (!IsBgra(frame)) return false;
    std::vector<std::uint8_t> hole =
        RectMask(frame.Width(), frame.Height(), rect);
    if (hole.empty()) return false;
    // RectMask produces 0/255; the impl treats >0 as hole.
    return InpaintMask(frame, hole, params);
}

bool InpaintMask(video::Frame& frame,
                 const std::vector<std::uint8_t>& hole_mask,
                 int search_radius) {
    InpaintParams params;
    params.search_radius = search_radius;
    return InpaintMask(frame, hole_mask, params);
}

bool InpaintMask(video::Frame& frame,
                 const std::vector<std::uint8_t>& hole_mask,
                 const InpaintParams& params) {
    if (!IsBgra(frame)) return false;
    if (hole_mask.size() !=
        static_cast<size_t>(frame.Width()) * frame.Height()) {
        return false;
    }
    std::vector<std::uint8_t> binary(hole_mask.size());
    for (size_t i = 0; i < hole_mask.size(); ++i) {
        binary[i] = hole_mask[i] > 0 ? 1 : 0;
    }
    return InpaintImpl(frame, binary, params);
}

}  // namespace XER::VideoEditor::Lab
