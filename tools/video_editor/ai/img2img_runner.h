// Tiled image->image runner for the VideoEditor AI features — the
// wave-2 infrastructure piece (docs/wiki/research/
// 2026-11-23-ai-features-wave2.md §W2) shared by the x4v3 upscale
// (ai/upscale_x4v3.h) and any later image->image model (depth,
// relight, ...). The runner is deliberately model-agnostic: callers
// inject a per-tile processor (usually "feed the tile through an
// Onnx::Session and hand back the block"), and this file owns the
// grid, the overlap/context padding and the seam blend — the three
// things every tiled model runner otherwise re-implements.
//
// Grid + blend contract:
//   * The frame is covered by tiles of `tile` source px on a step of
//     `tile - overlap`; the trailing tile in each row/column is
//     SHIFTED to end exactly at the frame edge, so every model feed
//     is `tile` px (or the frame itself when smaller — no
//     sub-contract edge sizes unless the frame demands them).
//   * Each tile's model output (rect * scale) is blended into the
//     result with a smoothstep weight ramp over exactly the zone it
//     shares with each neighbor: 0 at the shared edge -> 1 where the
//     tile is the sole coverer. Ramps are complementary by
//     construction (tile i fades in precisely where tile i-1 fades
//     out), corner zones get up to 4 contributors, and a per-pixel
//     weight-sum accumulator normalizes the blend, so seam energy
//     from any model lands as smooth falloff instead of a hard edge.
//   * Accumulation is exact integer math: u32 channels of
//     sum(px*wq) with wq = round(w*256), plus a u16 weight-sum —
//     deterministic and ~22 B/output-px all-in, so a 1080p -> 4320p
//     (x4) bounce peaks near 730 MB of scratch. That IS the
//     documented ceiling; x4 of 4K sources (~130 MP out) needs
//     ~2.9 GB and stays a bounce-scale tool, not a realtime path.
//
// Pure worker code like the rest of ai/ — the tile fn's session is
// thread-hostile; run on the bounce worker, never the UI thread.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace XER::VideoEditor::Ai {

struct Img2ImgParams {
    int tile = 256;      // model-feed tile edge, source px (>= 8)
    int overlap = 16;    // shared context zone per tile border,
                         // source px (0..tile/2 — wider breaks the
                         // 2-tiles-per-axis blend invariant)
    int scale = 4;       // output px per input px (x4v3 = 4)
};

// One tile's source rect (input px, inside the frame). The model
// output rect is rect * params.scale.
struct Img2ImgTile {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
};

// Computes the covering tile list for a `w` x `h` frame — the
// shifted-last-tile grid described above. Empty on degenerate args
// (w/h <= 0, tile < 8, overlap < 0 or overlap*2 > tile).
std::vector<Img2ImgTile> ComputeImg2ImgTiles(int w, int h,
                                             const Img2ImgParams& params);

// The model-side op the runner drives: reads the tile's BGRA source
// block (`tile_bgra`/`tile_stride` point INTO the source frame, no
// copy) and writes its processed BGRA block — `out_w` x `out_h` =
// tile rect * scale — into `out_bgra`/`out_stride` (dense scratch,
// valid for this call only). Returning false aborts the run with
// `error_out`.
using Img2ImgTileFn = std::function<bool(
    const std::uint8_t* tile_bgra, int tile_w, int tile_h,
    int tile_stride, std::uint8_t* out_bgra, int out_w, int out_h,
    int out_stride, std::string* error_out)>;

// Runs `process_tile` over the whole grid of `bgra` (w x h, BGRA8,
// `stride` >= w*4) and blends the tiles into `out_bgra` — a
// (w*scale) x (h*scale) BGRA8 frame (`out_w`/`out_h` report the
// dims). `progress` fires after every tile with the done fraction
// (0..1); `should_cancel` aborts between tiles — cancelled runs
// return false with "cancelled" in error_out, matching the ai/
// convention.
//
// False with error_out on: null/degenerate args, bad params (see
// ComputeImg2ImgTiles), a tile fn failure, or cancellation. On any
// failure `out_bgra` may be partially written — callers drop it.
bool RunImageToImage(const std::uint8_t* bgra, int w, int h,
                     int stride, const Img2ImgParams& params,
                     const Img2ImgTileFn& process_tile,
                     const std::function<void(float)>& progress,
                     const std::function<bool()>& should_cancel,
                     std::vector<std::uint8_t>* out_bgra, int* out_w,
                     int* out_h, std::string* error_out);

// The blend weight (0..1, smoothstep) of one output pixel inside one
// tile — exposed so the smoke can audit the ramp math without a
// model. `tile_*` is the tile's OUTPUT-space rect (source rect *
// scale), `px`/`py` the output pixel inside it; `fade_*` are the
// output-px widths of the shared zones on each side (0 = frame edge
// or unshared side -> weight 1 there).
float Img2ImgTileWeight(int px, int py, int tile_x0, int tile_y0,
                        int tile_w, int tile_h, int fade_l, int fade_t,
                        int fade_r, int fade_b);

}  // namespace XER::VideoEditor::Ai
