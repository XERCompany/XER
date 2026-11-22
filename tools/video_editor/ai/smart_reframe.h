// Smart Reframe subject tracking for the VideoEditor — the
// "auto-reframe" AI feature. ScanClipSubject samples the clip's
// consumed source span, runs the object detector on reduced-resolution
// frames, and records the PRIMARY subject (person preferred, else the
// largest confident box) as ClipReframe samples — the persisted,
// source-seconds subject track consumed by the compositor's
// xer.fx.smart_reframe path.
//
// Geometry lives here too so renderer, analyzer and tests share one
// implementation: ReframeWindowAt() turns the track + a target aspect
// into a clamped crop window at any source time, applying EMA +
// bounded-velocity smoothing that snaps across flagged scene
// boundaries instead of gliding through them.
//
// Pure worker/model code like the rest of ai/ — no UI state, no
// shared globals; drive scans from an AsyncOp job.
#pragma once

#include <functional>
#include <string>

#include "timeline_model.h"

namespace XER::VideoEditor::Ai {

// Effect id registered in builtin_fx.cc / the compositor reframe
// branch. Clip-side type spellings resolve through
// PluginHost::ForEffectType ("builtin:xer.fx.smart_reframe" is the
// canonical form).
inline constexpr const char* kSmartReframeId = "xer.fx.smart_reframe";

struct ReframeScanParams {
    double sample_step = 0.5;      // source seconds between probes
    double max_seconds = 600.0;    // source-span cap for very long media
    float confidence_threshold = 0.25f;
    float nms_threshold = 0.45f;
    int detect_width = 640;        // frames are downscaled to this width
                                   // for Detect() (0 = full resolution)
    float scene_threshold = 0.30f; // signature distance 0..1 => boundary
    double min_scene = 1.0;        // merge boundaries closer than this (s)
};

// Scans the clip's consumed source span [in_point, in_point +
// duration*rate) via the bridge decoder (same keyframe seek + bounded
// forward-discard loop as ScanClipForObjects). Each decoded sample is
// downscaled to ~detect_width, Detect()ed, and reduced to ONE primary
// subject: a "person" detection wins outright, otherwise the largest
// confident box. A 16x16 luma/channel signature marks samples that
// follow a hard cut (ReframeSample::scene_boundary). Samples land in
// `out` sorted by source time; undecodable probes are skipped.
//
// Returns false with error_out set when the model/backend is
// unavailable (see ai_tags.h FindObjectModel()), the media won't
// decode, or the span is empty. Worker-thread safe — owns no shared
// state; never call from the UI thread.
bool ScanClipSubject(const Clip& clip, const std::string& model_path,
                     const std::string& labels_path,
                     const ReframeScanParams& params,
                     const std::function<void(float)>& progress,
                     const std::function<bool()>& should_cancel,
                     ClipReframe* out, std::string* error_out);

// ---- render-side geometry -------------------------------------------------
// Axis-aligned crop window in FRAME PIXELS.
struct ReframeWindow {
    double x = 0.0, y = 0.0, w = 0.0, h = 0.0;
};

// Largest rect with aspect `ratio` (w/h) inscribed in
// frame_w x frame_h, centered.
ReframeWindow ReframeMaxWindow(int frame_w, int frame_h, double ratio);

// Crop window at source time `t`: the subject center is interpolated
// through the track's samples (empty track => frame center), run
// through causal EMA + bounded-velocity smoothing (smoothing 0..1
// scales both the EMA horizon and the max pan speed; samples flagged
// scene_boundary snap instead of gliding), then the largest
// ratio-aspect window is centered on it and clamped inside the frame.
ReframeWindow ReframeWindowAt(const ClipReframe& track, double t,
                              int frame_w, int frame_h, double ratio,
                              double smoothing);

}  // namespace XER::VideoEditor::Ai
