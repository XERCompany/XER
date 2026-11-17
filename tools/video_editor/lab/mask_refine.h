// Mask Refine — Photoshop-style "Refine Edge" operations on a
// single-channel coverage mask, plus frame<->mask alpha plumbing for
// the Object Lab / inspector. A mask is w*h values, row-major: float
// masks use [0,1] coverage; uint8 masks use 0..255 (the same shape
// object_ops' RectMask/PolygonMask produce, so these compose
// directly).
//
//   ExtractAlphaMask / ApplyAlphaMask — BGRA8 alpha <-> float mask
//   FeatherMaskInPlace   — Gaussian-ish edge softening (blur radius)
//   GrowMaskInPlace      — shift edge: +px dilates, -px erodes
//   EdgeContrastInPlace  — steepen/flatten the edge ramp about 0.5
//   RefineMaskEdgeInPlace— one-call composite: grow -> feather ->
//                          contrast (Photoshop's slider order)
//
// Every op is in place, clamped to [0,1] (or 0..255), and a no-op on
// mismatched sizes / empty masks / zero effect strength. Feather is
// dot-product-shaped per row and routes through Lab::Sdot
// (mkl_accel.h) — MKL/OpenBLAS accelerate it when probed, the scalar
// backend computes the identical result.
#pragma once

#include <cstdint>
#include <vector>

#include "video/frame.h"

namespace XER::VideoEditor::Lab {

// ------------------------------------------------------------------
// Frame <-> mask
// ------------------------------------------------------------------

// Alpha plane of a BGRA8 frame as a w*h float mask in [0,1].
// Non-BGRA8 / invalid frames yield an empty vector.
std::vector<float> ExtractAlphaMask(const video::Frame& frame);

// Writes `alpha` (w*h floats, clamped to [0,1]) into the frame's
// alpha channel; BGR untouched. False on a non-BGRA8 frame or a size
// mismatch (frame left untouched in that case).
bool ApplyAlphaMask(video::Frame& frame, const std::vector<float>& alpha);

// ------------------------------------------------------------------
// Float masks ([0,1])
// ------------------------------------------------------------------

// Edge softening: separable Gaussian-approximation blur with a
// +-radius_px kernel (sigma = radius/2, replicate-border). Deep
// interior/exterior coverage is preserved; the ramp widens to roughly
// 2*radius_px. radius_px <= ~0 is a no-op.
void FeatherMaskInPlace(std::vector<float>& alpha, int w, int h,
                        float radius_px);

// Photoshop "Shift Edge": radius_px > 0 dilates coverage by that many
// pixels (separable square max-filter), radius_px < 0 erodes (min-
// filter). Works on soft masks too — intermediate coverage shifts
// with the edge. 0 is a no-op.
void GrowMaskInPlace(std::vector<float>& alpha, int w, int h,
                     float radius_px);

// Reshapes the edge ramp about the 50% point: v' = 0.5 + (v-0.5)*gain
// clamped to [0,1], where gain = 10^contrast — contrast in [-1,1],
// +1 approaches a hard edge, -1 flattens toward 0.5, 0 is a no-op.
void EdgeContrastInPlace(std::vector<float>& alpha, int w, int h,
                        float contrast);

// Composite Refine Edge in Photoshop's slider order:
// GrowMaskInPlace(shift_px) -> FeatherMaskInPlace(feather_px) ->
// EdgeContrastInPlace(contrast). Each zero slider skips its stage.
void RefineMaskEdgeInPlace(std::vector<float>& alpha, int w, int h,
                           float shift_px, float feather_px,
                           float contrast);

// ------------------------------------------------------------------
// uint8 masks (0..255 — object_ops-compatible)
// ------------------------------------------------------------------
// Same semantics as the float variants; values are converted through
// the float path and rounded back, so a 0-strength call is still a
// bit-exact no-op.

void FeatherMaskInPlace(std::vector<std::uint8_t>& mask, int w, int h,
                        float radius_px);
void GrowMaskInPlace(std::vector<std::uint8_t>& mask, int w, int h,
                     float radius_px);
void EdgeContrastInPlace(std::vector<std::uint8_t>& mask, int w, int h,
                         float contrast);
void RefineMaskEdgeInPlace(std::vector<std::uint8_t>& mask, int w,
                           int h, float shift_px, float feather_px,
                           float contrast);

}  // namespace XER::VideoEditor::Lab
