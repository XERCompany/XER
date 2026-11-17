// Object Lab — Photoshop-style object operations on video::Frame
// (packed BGRA8). UI-free and decoder-free: the app hands in the
// current monitor frame and places the returned frame itself
// (e.g. "перенос на новый слой" = CopyRegionToLayer output dropped on
// a new clip layer at the same canvas position).
//
//   CutOutRect        — rect extraction, same-size frame, rect opaque
//   CutOutLasso       — polygon lasso w/ linear feathered edge
//   InpaintRect/Mask  — exemplar-based fill ("remove object")
//   CopyRegionToLayer — masked pixels + alpha, else transparent
//
// All ops require PixelFormat::BGRA8; anything else returns an invalid
// Frame / false. Coordinates are frame pixels, rect/polygon are
// clamped to bounds.
#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include "video/frame.h"

namespace XER::VideoEditor::Lab {

struct IntPoint {
    int x = 0;
    int y = 0;
};

struct IntRect {
    int x = 0;
    int y = 0;
    int w = 0;   // width in pixels (>0 to be useful)
    int h = 0;
};

// ------------------------------------------------------------------
// Masks — w*h bytes, 0..255 coverage, row-major.
// ------------------------------------------------------------------

// Hard binary mask of the clamped rect (255 inside, 0 outside).
std::vector<std::uint8_t> RectMask(int frame_w, int frame_h, IntRect rect);

// Polygon mask with a linear feather centered on the edge:
// coverage = clamp(0.5 + signed_distance / feather_px), so the ramp
// spans feather_px pixels half-in/half-out. feather_px <= 0 gives a
// hard edge. Even-odd fill; self-intersecting lassos are handled.
// Empty polygon (<3 points) or empty frame -> empty vector.
std::vector<std::uint8_t> PolygonMask(int frame_w, int frame_h,
                                      const std::vector<IntPoint>& polygon,
                                      int feather_px);

// ------------------------------------------------------------------
// Cut / copy
// ------------------------------------------------------------------

// "Layer via Cut" on a rect: returns a NEW frame the same size as the
// source. Pixels inside the clamped rect are copied with alpha forced
// to 255; everything outside is fully transparent (BGRA 0). An empty
// or fully out-of-bounds rect returns an invalid Frame.
video::Frame CutOutRect(const video::Frame& frame, IntRect rect);

// Same-size cutout driven by a lasso polygon instead of a rect.
// Alpha = feathered coverage * 255; BGR copied wherever coverage > 0
// so the soft edge blends correctly. Invalid Frame when the polygon
// has < 3 points or covers nothing.
video::Frame CutOutLasso(const video::Frame& frame,
                         const std::vector<IntPoint>& polygon,
                         int feather_px);

// The "copy to new layer" payload: same-size frame with the masked
// pixels (BGR + alpha = mask value, so feathered masks stay soft) and
// everything else fully transparent. mask must be w*h bytes; a wrong
// size returns an invalid Frame.
video::Frame CopyRegionToLayer(const video::Frame& frame,
                               const std::vector<std::uint8_t>& mask);

// ------------------------------------------------------------------
// Inpainting ("remove object")
// ------------------------------------------------------------------

struct InpaintParams {
    int search_radius = 24;     // px; source pixels searched within +/-r
    int patch_radius = 3;       // patch = (2r+1)^2, default 7x7
    int iterations = 6;         // propagation/refinement sweeps
    int random_candidates = 8;  // random offsets sampled per pixel/sweep
    std::uint32_t seed = 0x9E3779B9u;  // deterministic runs for tests
    // Optional async plumbing (see async_op.h): poll cancel inside the
    // sweep loop, report 0..1 after every sweep. A cancelled run
    // returns false and leaves the frame untouched.
    std::function<bool()> should_cancel;
    std::function<void(float)> report_progress;
};

// Fills the rect hole in place. Returns false on a bad frame, an empty
// hole, a hole covering the whole frame (nothing to sample from), or
// cancellation (frame untouched in that case).
bool InpaintRect(video::Frame& frame, IntRect rect, int search_radius);
bool InpaintRect(video::Frame& frame, IntRect rect,
                 const InpaintParams& params);

// Same engine driven by an arbitrary hole mask (w*h bytes, >0 = hole)
// — e.g. the lasso mask above, so freehand removals work too.
bool InpaintMask(video::Frame& frame,
                 const std::vector<std::uint8_t>& hole_mask,
                 int search_radius);
bool InpaintMask(video::Frame& frame,
                 const std::vector<std::uint8_t>& hole_mask,
                 const InpaintParams& params);

}  // namespace XER::VideoEditor::Lab
