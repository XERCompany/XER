// Headless timeline model for the VideoEditor core (XER Studio
// Phase 1B). Pure data + synchronous, UI-thread-owned mutations — no
// threads, no async, no engine objects inside Project. Media bounds are
// passed per-call via MediaBounds so the model stays decoupled from the
// decoder/catalog.
//
// Invariants enforced by the mutating ops (all validate-then-mutate,
// transactional — a rejected op leaves the project untouched):
//   * clip.track indexes Project::tracks and is in range
//   * audio-only media (MediaBounds::has_video == false) only on
//     TrackKind::Audio tracks
//   * clip ids are non-empty and unique
//   * no two clips overlap on the same track (adjacency allowed)
//   * transitions only between adjacent clips on the same VIDEO track
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace XER::VideoEditor {

enum class TrackKind { Video, Audio };

struct Transform {
    float offset_x = 0.0f;      // clip top-left, fraction of canvas w/h
    float offset_y = 0.0f;
    float scale = 1.0f;         // uniform multiplier on source pixels
    float rotation_deg = 0.0f;
    float opacity = 1.0f;
};
struct ClipEffect { std::string type; std::vector<float> params; };
struct Clip {
    std::string id;             // uuid (NewClipId)
    std::string source_path;    // UTF-8
    int track = 0;              // index into Project::tracks
    double timeline_start = 0;  // seconds
    double in_point = 0;        // source seconds
    double duration = 0;        // timeline seconds; out = in + duration
    bool muted = false;
    float gain_db = 0;
    double fade_in = 0, fade_out = 0;   // seconds
    Transform transform;
    std::vector<ClipEffect> effects;
};
struct Transition {
    std::string from_clip, to_clip;     // adjacent on same VIDEO track
    enum class Type {
        Crossfade, WipeLeft, WipeRight, WipeUp, WipeDown,
        SlideLeft, SlideRight, SlideUp, SlideDown
    } type = Type::Crossfade;
    double duration = 1.0;              // seconds, centered on the cut
};
struct Track {
    TrackKind kind = TrackKind::Video;
    std::string name;
    bool muted = false;    // audio: silence; video: ignored (use hidden)
    bool hidden = false;   // video: not composited (+ its audio); ignored on audio
    float gain_db = 0;     // stored for Phase 2
};
struct Project {
    int width = 1920, height = 1080;
    int fps_num = 30, fps_den = 1;
    std::array<std::uint8_t,4> bg_color{0,0,0,255};
    std::vector<Track> tracks;
    std::vector<Clip> clips;
    std::vector<Transition> transitions;
    double duration() const;   // max(timeline_start + duration), 0 if empty
};

// Source bounds for invariant checks. duration_seconds <= 0 means
// "unknown" → no upper clamp; has_video == false restricts the clip to
// audio tracks.
struct MediaBounds {
    double duration_seconds = 0.0;
    bool has_video = true;
};

enum class TrimEdge { In, Out };

// ---- queries ----
const Clip*  FindClip(const Project&, const std::string& clip_id);
Clip*        FindClip(Project&, const std::string& clip_id);
const Track* FindTrack(const Project&, int track_index);
double       ClipEndSeconds(const Clip&);                    // start + duration
bool         ClipActiveAt(const Clip&, double t);            // start <= t < end
// Strict [start, end) interval overlap — adjacency (end == start) is
// NOT overlap. Track equality is the caller's concern.
bool         ClipsOverlap(const Clip& a, const Clip& b);
std::vector<const Clip*> ClipsOnTrack(const Project&, int track_index);  // by start
std::vector<Clip*>       ClipsOnTrack(Project&, int track_index);
// Sorted unique snap candidates: 0, every clip start/end on
// `track_index` (all tracks when < 0), plus `playhead` when >= 0.
std::vector<double> CollectSnapPoints(const Project&, int track_index,
                                      double playhead);
// `candidate` snapped to the nearest snap point within `threshold`
// seconds (ties → earlier point), else `candidate` unchanged.
double SnapTime(const Project&, int track_index, double candidate,
                double threshold, double playhead);

// ---- mutations (validate-then-mutate; transactional; error_out) ----
// Shared placement validation: track index valid; has_video==false
// requires TrackKind::Audio; id non-empty/unique (unless it equals
// exclude_clip_id); duration > kMinClipDuration; start >= 0;
// in_point >= 0; in_point + duration <= media duration when known;
// no overlap on the track excluding exclude_clip_id.
bool ValidateClipPlacement(const Project&, const Clip&, const MediaBounds&,
                           const std::string& exclude_clip_id,
                           std::string* error_out);

int  AddTrack(Project&, TrackKind kind, const std::string& name);  // index or -1
// Only when the track holds no clips; reindexes clip.track > index.
bool RemoveTrack(Project&, int track_index, std::string* error_out);

bool AddClip(Project&, const Clip&, const MediaBounds&, std::string* error_out);
bool RemoveClip(Project&, const std::string& clip_id);
bool MoveClip(Project&, const std::string& clip_id, double new_timeline_start,
              const MediaBounds&, std::string* error_out);
// TrimEdge::In  — new_point is a new in_point (source s);
//   timeline_start += (new_in - old_in); duration = old_out - new_in.
// TrimEdge::Out — new_point is a new out_point (source s);
//   duration = new_out - in_point; timeline_start unchanged.
bool TrimClip(Project&, const std::string& clip_id, TrimEdge edge,
              double new_point, const MediaBounds&, std::string* error_out);
// split_timeline_t strictly inside (start + kMinClipDuration,
// end - kMinClipDuration). First half keeps `clip_id`; second half gets
// `new_clip_id` (caller-supplied, non-empty, unique). Second half:
// in_point += split_t - start; timeline_start = split_t;
// duration = end - split_t. Both keep muted/gain/transform/effects;
// fade_in stays on first half (second gets 0), fade_out stays on second
// (first gets 0). Transitions with to_clip==id keep pointing at the
// first half; transitions with from_clip==id are re-pointed to
// new_clip_id.
bool SplitClip(Project&, const std::string& clip_id, double split_timeline_t,
               const std::string& new_clip_id, std::string* error_out);
// Inspector/track-move edits: replacement.id is ignored — the stored
// clip keeps `clip_id`; all other fields (incl. track) come from
// `replacement` and are re-validated.
bool ReplaceClip(Project&, const std::string& clip_id, const Clip& replacement,
                 const MediaBounds&, std::string* error_out);
bool AddTransition(Project&, const std::string& from_clip,
                   const std::string& to_clip, Transition::Type type,
                   double duration, std::string* error_out);
bool RemoveTransition(Project&, const std::string& from_clip,
                      const std::string& to_clip);
// Removes every transition that is not video-track, missing an
// endpoint, on different tracks, or non-adjacent (to.start ≈
// from.end). Called by every mutating op and by project_store load.
void DropInvalidTransitions(Project&);

// Centered letterbox fit: scale = min(canvas_w/src_w,
// canvas_h/src_h); offset = (canvas - src*scale)/2 as a fraction of
// canvas w/h. Returns identity Transform{} when any dim <= 0.
Transform FitTransformToCanvas(int src_w, int src_h, int canvas_w,
                               int canvas_h);
std::string NewClipId();   // uuid v4, "8-4-4-4-12" hex

}  // namespace XER::VideoEditor
