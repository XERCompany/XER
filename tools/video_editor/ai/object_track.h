// "Track object → pin content" for the VideoEditor — the object-pin
// AI feature. ScanClipTrack reuses the generalized detection scan
// (ai/smart_reframe.h ScanClipDetections) to record a per-frame
// smoothed bounding box on Clip::object_track (.xerproj-serialized,
// source seconds / frame fractions). Two consumers read the track:
//
//   * Pin → Title: BakePinKeys() converts the track into PosX/PosY
//     keyframes for a generated text clip — BAKED data (undoable,
//     editable), not a live link.
//   * Pin → Mosaic: the builtin:xer.fx.track_mosaic effect — a LIVE
//     reference like the smart-reframe warp hook: the compositor
//     resolves the tracked box at each frame's source time and
//     pixelates it, so re-analysis/refinement flows through without
//     touching the effect record.
//
// Pure worker/model code — no UI state; drive scans from an AsyncOp
// job like the rest of ai/.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "ai/smart_reframe.h"
#include "timeline_model.h"

namespace XER::VideoEditor::Ai {

// Effect id registered in builtin_fx.cc. The compositor skips the
// generic ABI path for it (its registered fn is identity) and applies
// the pixelated region itself — params carry only {block px, padding
// fraction, smoothing}; the tracked box comes from
// Clip::object_track at the frame's source time.
inline constexpr const char* kTrackMosaicId = "xer.fx.track_mosaic";

// Scans the clip's consumed source span through ScanClipDetections.
// `label_filter` empty = the shared primary-subject policy (person
// preferred, else largest confident box); non-empty = prefer the
// highest-confidence detection whose label matches (case-insensitive;
// also accepts "class_<id>" spellings), falling back to the primary
// subject at probes where the class is absent — gaps bridge the same
// way misses do.
//
// `out` gets the picked samples plus `label` (the dominant picked
// class) and `classes` (every distinct label observed across the
// span — the detection candidates a "pick object" UI can offer for
// re-tracks). Same error contract as ScanClipSubject.
bool ScanClipTrack(const Clip& clip, const std::string& model_path,
                   const std::string& labels_path,
                   const ReframeScanParams& params,
                   const std::string& label_filter,
                   const std::function<void(float)>& progress,
                   const std::function<bool()>& should_cancel,
                   ClipTrack* out, std::string* error_out);

// ---- render-side geometry ------------------------------------------
// Smoothed tracked box at source time t, frame fractions: the center
// follows the shared causal EMA + bounded-velocity curve
// (TrackCenterAt — scene-boundary snap included); w/h lerp between
// neighbouring raw samples (size changes are slower than motion, so
// they need no velocity cap). Ends hold; an empty track returns a
// degenerate zero-size box — callers treat w <= 0 as "nothing
// tracked" (the mosaic no-ops, pin baking rejects).
struct TrackBox {
    double cx = 0.5;
    double cy = 0.5;
    double w = 0.0;
    double h = 0.0;
};
TrackBox TrackBoxAt(const ClipTrack& track, double t, double smoothing);

// ---- pin → title baking --------------------------------------------
// Which point of the pinned clip lands on the tracked box:
//   TopCenter    — pin's bottom-center rests `margin` above the box
//                  top (the classic "name tag above the subject").
//   Center       — pin's center sits on the box center (badge overlay).
//   BottomCenter — pin's top-center rests `margin` below the box
//                  bottom (subtitle/callout below the subject).
enum class PinAnchor { TopCenter, Center, BottomCenter };

// Geometry inputs for BakePinKeys: the tracked clip's DECODED frame
// dims (catalog probe; fractions alone can't carry the source->
// canvas mapping when the clip's transform is not identity), the
// project canvas dims, and the pinned clip's intrinsic frame size at
// its transform.scale — measured via GenerateSourceFrame for text.
struct PinGeometry {
    int src_w = 0, src_h = 0;
    int canvas_w = 0, canvas_h = 0;
    double pin_w = 0.0, pin_h = 0.0;
    double pin_scale = 1.0;
    double margin = 0.02;   // canvas-height fraction above/below box
};

// PosX/PosY key lists (clip-local seconds, Linear easing) pinning the
// described clip to `tracked.object_track` through `anchor`.
struct PinKeys {
    std::vector<Keyframe> pos_x;
    std::vector<Keyframe> pos_y;
};

// Bakes the track into canvas-fraction position keys (Transform
// semantics: offset = the pinned frame's top-left as a fraction of
// canvas w/h). Per track sample the tracked box's anchor point is
// mapped SOURCE-fraction -> canvas-fraction through the tracked
// clip's EFFECTIVE transform at the sample's clip-local time
// (EffectiveTransformAt — keyframed source motion is followed; the
// smart-reframe warp and rotation are not — a rotated/reframed
// source pins at the unrotated box position, documented limitation).
// Sample source times map to clip-local key times via
// u = (t - in_point)/rate; keys outside [0, duration] are dropped.
// `smoothing` runs the shared TrackCenterAt curve so the baked path
// glides exactly like the live mosaic follows.
//
// Returns false with error_out when the track is empty, the geometry
// degenerate, or no sample lands inside the clip's consumed span.
bool BakePinKeys(const Clip& tracked, PinAnchor anchor,
                 const PinGeometry& geom, double smoothing,
                 PinKeys* out, std::string* error_out);

}  // namespace XER::VideoEditor::Ai
