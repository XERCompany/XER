// SAM2.1 mask tracking for the VideoEditor — "Track Mask (SAM)…"
// (docs/wiki/research/2026-11-23-hf-org-scan.md §1: the onnx-community
// export is IMAGE-MODE — no video-memory graphs exist — so tracking
// is per-frame re-prompt through the same probe-scan cadence the
// ScanClipFrames/object_track seam established):
//
//   * the user picks a point (armed monitor click — the existing
//     SetClickPointsArmed seam) or a box on frame T0;
//   * T0 is segmented from that prompt;
//   * each subsequent probe re-prompts the decoder with the PREVIOUS
//     mask's bounding box + centroid point — the drift-compensated
//     propagation the research page prescribes;
//   * drift control: a probe whose chosen-head IoU or mask-vs-prev
//     IoU collapses is a MISS — a "coast" sample repeats the last
//     good mask (scene_boundary, so the interpolator snaps instead
//     of gliding) and up to max_misses consecutive misses are
//     bridged before the track stops dead;
//   * every kept sample lands on Clip::mask_track as the bbox
//     (frame fractions, same convention as ReframeSample) + a
//     packed 1bpp bitmap on a fixed grid.
//
// The compositor's xer.fx.track_mask branch resolves the mask at
// each frame's source time (same live-reference contract as
// xer.fx.track_mosaic) and isolates / blurs-behind / keys on it.
//
// Pure worker/model code like the rest of ai/ — drive the scan
// from an async job (bounce_worker pattern), never the UI thread.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "ai/object_track.h"   // TrackBox
#include "ai/sam2_session.h"
#include "ai/smart_reframe.h"  // ReframeScanParams
#include "timeline_model.h"

namespace XER::VideoEditor::Ai {

// Effect id registered in builtin_fx.cc. Same dispatch shape as
// xer.fx.track_mosaic: the ABI fn is identity — the compositor
// resolves the mask from Clip::mask_track at the frame's source
// time. Params {mode, feather, smoothing}: mode 0 = "isolate"
// (keep masked region, alpha out the rest), 1 = "blur_bg" (blur
// everything BUT the mask), 2 = "key" (mask as alpha).
inline constexpr const char* kTrackMaskId = "xer.fx.track_mask";

// Default packed-mask grid edge (width cells — height follows the
// probed frame aspect, square-ish cells, clamped [8, 128]).
inline constexpr int kSam2TrackGridW = 64;

struct Sam2TrackParams {
    ReframeScanParams scan;     // cadence + span cap (sample_step,
                                // max_seconds); detect_width is
                                // overridden to <=1024 — the encoder
                                // stretches to its 1024 square anyway
    // ---- T0 prompt (frame FRACTIONS 0..1 — the armed click maps
    // into this domain; converted to probe px at the first frame) --
    std::vector<float> prompt_points;   // x,y pairs
    std::vector<int> prompt_labels;     // 1 fg / 0 bg (same count)
    bool has_box = false;
    float box_norm[4] = {0.0f, 0.0f, 0.0f, 0.0f};  // x1,y1,x2,y2
    // Source seconds of T0 — the probe the prompt applies to;
    // < 0 -> clip.in_point (whole-span track from its first probe).
    double start_seconds = -1.0;
    // ---- drift control ----
    float min_iou_score = 0.35f;  // chosen head's iou_scores floor
    float min_mask_iou = 0.05f;   // mask-vs-prev-grid IoU floor
    int max_misses = 3;           // consecutive misses before stop
    // ---- mask grid ----
    int grid_w = kSam2TrackGridW; // packed-bitmap width (cells)
    int grid_h = 0;               // 0 -> aspect-derived square cells
};

// Runs the per-frame re-prompt track: decode probes forward from
// `start_seconds` at scan.sample_step cadence; the FIRST probe
// consumes the user prompt, every later probe re-prompts from the
// previous good mask (bbox + centroid). Samples + grid land in
// `out` (label "sam"). Same error contract as ScanClipFaces.
bool ScanClipMaskTrack(
    const Clip& clip, const std::string& encoder_path,
    const std::string& decoder_path, const Sam2TrackParams& params,
    const std::function<void(float)>& progress,
    const std::function<bool()>& should_cancel, ClipMaskTrack* out,
    std::string* error_out);

// ---- render-side mask resolution -----------------------------------
// The tracked mask at source time `t`, unpacked to an A8 coverage
// grid (gw*gh bytes, 0 or 255 — bilinear-resampled later by the
// caller). Between samples the two bitmaps CROSSFADE by linear
// weight; a sample flagged scene_boundary holds the earlier sample
// until its own t (snap semantics — same convention as the track
// smoother). Ends hold; outside the sampled span and on an empty
// track the coverage is empty (grid stays at its dims, a8 cleared).
// `box_out` (optional) receives the interpolated bbox fractions.
// Returns false when the track/grid is degenerate — the compositor
// treats that as "nothing tracked" (fx no-op).
bool MaskTrackAt(const ClipMaskTrack& track, double t,
                 std::vector<std::uint8_t>* a8_out,
                 TrackBox* box_out = nullptr);

// ---- click seam -----------------------------------------------------
// Maps a program-canvas point (normalized nx,ny) into the clip's
// SOURCE-frame fractions through the clip's effective transform at
// clip-local time `u` — the inverse of WarpClipFrame's map (dest =
// src_w*scale x src_h*scale top-left at offset*canvas, inverse
// rotation about the rect center). `src_w`/`src_h` are the clip's
// decoded frame dims (catalog probe). The smart-reframe warp is NOT
// followed: on a reframed clip the point resolves against the
// un-cropped frame (same documented caveat as BakePinKeys — the
// prompt is still correct in source space; only clicks on a
// REFRAMED view land slightly off). False when the point falls
// outside the clip's warped rect or the transform degenerates.
bool CanvasPointToSourceFraction(const Clip& clip, double u,
                                 int canvas_w, int canvas_h,
                                 int src_w, int src_h,
                                 double nx, double ny,
                                 float* fx, float* fy);

}  // namespace XER::VideoEditor::Ai
