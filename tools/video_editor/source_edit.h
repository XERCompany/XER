// Source-monitor edit helpers for the VideoEditor core (XER Studio
// Phase 1E — Premiere-style source monitor). Headless and UI-free so
// the marked in/out -> insert/overwrite path stays unit-testable
// without the shell: the app keeps a SourceMarks entry per media path,
// resolves it here against the probed duration, builds the Clip, and
// dispatches to the timeline model's InsertClipAt / OverwriteClipAt.
#pragma once

#include <string>

#include "timeline_model.h"

namespace XER::VideoEditor {

// In/out marks on a media file, in SOURCE seconds. A negative value
// means "not set" — Premiere's unmarked side resolves against the
// media bounds (in -> 0, out -> duration).
struct SourceMarks {
    double in = -1.0;
    double out = -1.0;
    bool HasIn() const { return in >= 0.0; }
    bool HasOut() const { return out >= 0.0; }
};

// Resolves marks + media duration into a usable [in, out) source
// range: unmarked in -> 0; unmarked out -> media_duration (or
// in + kFallbackSeconds when the duration is unknown); both clamped to
// [0, media_duration] when the duration is known. A resolved out that
// does not exceed in by at least min_duration returns false.
// min_duration mirrors the model's kMinClipDuration.
bool ResolveSourceRange(const SourceMarks& marks, double media_duration,
                        double* in_out, double* out_out,
                        double min_duration = 0.001);

// Fallback span used when a clip has no out mark AND the media
// duration is unknown (same 5 s convention AddMediaToTimeline uses).
inline constexpr double kSourceFallbackSeconds = 5.0;

// Builds the timeline Clip a source edit would place: source_path on
// `track` at `timeline_start`, in_point = src_in, duration =
// src_out - src_in, playback_rate 1. The caller assigns the id. A
// fit-to-canvas transform is stamped only when both source and canvas
// dims are positive (audio-only media keeps the identity).
Clip MakeSourceClip(const std::string& path, int track,
                    double timeline_start, double src_in, double src_out,
                    int src_w, int src_h, int canvas_w, int canvas_h);

// Three-point edit dispatch: insert = ripple (InsertClipAt — clips at
// or after timeline_start on the clip's track shift right),
// overwrite = replace (OverwriteClipAt — the span is cleared first).
// Returns the model op's result verbatim.
bool SourceEditAt(Project& project, const Clip& clip,
                  const MediaBounds& bounds, bool insert,
                  std::string* error_out);

}  // namespace XER::VideoEditor
