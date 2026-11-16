// Implementation of the headless timeline model — see timeline_model.h
// for the data model and the mutation contract. Everything here is
// synchronous and UI-thread-owned; Project is a plain value type.

#include "timeline_model.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <limits>
#include <random>

namespace XER::VideoEditor {
namespace {

// 1 ms floor — the spec sets no minimum clip length; this only rejects
// degenerate (zero/negative/sub-millisecond) clips.
constexpr double kMinClipDuration = 0.001;
// Adjacency / snap tolerance in seconds.
constexpr double kEpsilon = 1e-6;

// Shared failure path for the mutating ops: writes a message when the
// caller asked for one and returns false.
bool Fail(std::string* error_out, const char* message) {
    if (error_out != nullptr) {
        *error_out = message;
    }
    return false;
}

// A transition is structurally valid iff both endpoints exist, sit on
// the same track, that track is a VIDEO track, and the clips are
// adjacent (to.start ≈ from.end). Under the no-overlap invariant exact
// boundary contact is sufficient — nothing can sit in between.
bool TransitionEndpointsAdjacent(const Project& project,
                                 const Transition& transition) {
    const Clip* from = FindClip(project, transition.from_clip);
    const Clip* to = FindClip(project, transition.to_clip);
    if (from == nullptr || to == nullptr) {
        return false;
    }
    if (from->track != to->track) {
        return false;
    }
    if (from->track < 0 ||
        from->track >= static_cast<int>(project.tracks.size())) {
        return false;
    }
    if (project.tracks[from->track].kind != TrackKind::Video) {
        return false;
    }
    return std::fabs(to->timeline_start - ClipEndSeconds(*from)) <=
           kEpsilon;
}

}  // namespace

double Project::duration() const {
    double end = 0.0;
    for (const auto& clip : clips) {
        end = std::max(end, ClipEndSeconds(clip));
    }
    return end;
}

const Clip* FindClip(const Project& project, const std::string& clip_id) {
    for (const auto& clip : project.clips) {
        if (clip.id == clip_id) {
            return &clip;
        }
    }
    return nullptr;
}

Clip* FindClip(Project& project, const std::string& clip_id) {
    for (auto& clip : project.clips) {
        if (clip.id == clip_id) {
            return &clip;
        }
    }
    return nullptr;
}

const Track* FindTrack(const Project& project, int track_index) {
    if (track_index < 0 ||
        track_index >= static_cast<int>(project.tracks.size())) {
        return nullptr;
    }
    return &project.tracks[static_cast<std::size_t>(track_index)];
}

double ClipEndSeconds(const Clip& clip) {
    return clip.timeline_start + clip.duration;
}

bool ClipActiveAt(const Clip& clip, double t) {
    return clip.timeline_start <= t && t < ClipEndSeconds(clip);
}

bool ClipsOverlap(const Clip& a, const Clip& b) {
    return a.timeline_start < ClipEndSeconds(b) &&
           b.timeline_start < ClipEndSeconds(a);
}

std::vector<const Clip*> ClipsOnTrack(const Project& project,
                                      int track_index) {
    std::vector<const Clip*> out;
    for (const auto& clip : project.clips) {
        if (clip.track == track_index) {
            out.push_back(&clip);
        }
    }
    std::sort(out.begin(), out.end(),
              [](const Clip* a, const Clip* b) {
                  return a->timeline_start < b->timeline_start;
              });
    return out;
}

std::vector<Clip*> ClipsOnTrack(Project& project, int track_index) {
    std::vector<Clip*> out;
    for (auto& clip : project.clips) {
        if (clip.track == track_index) {
            out.push_back(&clip);
        }
    }
    std::sort(out.begin(), out.end(),
              [](const Clip* a, const Clip* b) {
                  return a->timeline_start < b->timeline_start;
              });
    return out;
}

std::vector<double> CollectSnapPoints(const Project& project,
                                      int track_index, double playhead) {
    std::vector<double> points;
    points.push_back(0.0);
    for (const auto& clip : project.clips) {
        if (track_index >= 0 && clip.track != track_index) {
            continue;
        }
        points.push_back(clip.timeline_start);
        points.push_back(ClipEndSeconds(clip));
    }
    if (playhead >= 0.0) {
        points.push_back(playhead);
    }
    std::sort(points.begin(), points.end());
    points.erase(std::unique(points.begin(), points.end(),
                             [](double a, double b) {
                                 return std::fabs(a - b) <= kEpsilon;
                             }),
                 points.end());
    return points;
}

double SnapTime(const Project& project, int track_index, double candidate,
                double threshold, double playhead) {
    const auto points = CollectSnapPoints(project, track_index, playhead);
    double best = candidate;
    double best_distance = std::numeric_limits<double>::infinity();
    for (const double point : points) {
        const double distance = std::fabs(point - candidate);
        // Strict < keeps the earlier point on ties (points are sorted).
        if (distance < best_distance) {
            best_distance = distance;
            best = point;
        }
    }
    return best_distance <= threshold ? best : candidate;
}

bool ValidateClipPlacement(const Project& project, const Clip& clip,
                           const MediaBounds& bounds,
                           const std::string& exclude_clip_id,
                           std::string* error_out) {
    if (clip.track < 0 ||
        clip.track >= static_cast<int>(project.tracks.size())) {
        return Fail(error_out, "clip track index out of range");
    }
    if (!bounds.has_video &&
        project.tracks[static_cast<std::size_t>(clip.track)].kind !=
            TrackKind::Audio) {
        return Fail(error_out,
                    "audio-only media can only be placed on an audio track");
    }
    if (clip.id.empty()) {
        return Fail(error_out, "clip id is empty");
    }
    if (clip.id != exclude_clip_id &&
        FindClip(project, clip.id) != nullptr) {
        return Fail(error_out, "clip id already exists");
    }
    // Every comparison below is relational — NaN makes them all false,
    // so finiteness is checked explicitly first. Non-finite values
    // would also persist: Save emits NaN/Infinity literals that Load's
    // strict parser rejects, leaving a .xerproj the model cannot
    // reload.
    if (!std::isfinite(clip.duration)) {
        return Fail(error_out, "clip duration must be finite");
    }
    if (clip.duration <= kMinClipDuration) {
        return Fail(error_out, "clip duration is too small");
    }
    if (!std::isfinite(clip.timeline_start)) {
        return Fail(error_out, "clip start must be finite");
    }
    if (clip.timeline_start < 0.0) {
        return Fail(error_out, "clip start is before zero");
    }
    if (!std::isfinite(clip.in_point)) {
        return Fail(error_out, "clip in-point must be finite");
    }
    if (clip.in_point < 0.0) {
        return Fail(error_out, "clip in-point is before zero");
    }
    // Finite fields can still overflow to ±inf when summed — reject
    // extents that are not representable for the same reason.
    if (!std::isfinite(clip.in_point + clip.duration) ||
        !std::isfinite(ClipEndSeconds(clip))) {
        return Fail(error_out, "clip extent is not representable");
    }
    if (bounds.duration_seconds > 0.0 &&
        clip.in_point + clip.duration >
            bounds.duration_seconds + kEpsilon) {
        return Fail(error_out, "clip exceeds the media duration");
    }
    // Presentation fields ride along in the same Clip and persist
    // through Save — they get the same finiteness contract so a
    // ReplaceClip/AddClip cannot smuggle NaN into a .xerproj either.
    if (!std::isfinite(clip.gain_db)) {
        return Fail(error_out, "clip gain must be finite");
    }
    if (!std::isfinite(clip.fade_in) || !std::isfinite(clip.fade_out)) {
        return Fail(error_out, "clip fades must be finite");
    }
    const Transform& transform = clip.transform;
    if (!std::isfinite(transform.offset_x) ||
        !std::isfinite(transform.offset_y) ||
        !std::isfinite(transform.scale) ||
        !std::isfinite(transform.rotation_deg) ||
        !std::isfinite(transform.opacity)) {
        return Fail(error_out, "clip transform must be finite");
    }
    for (const ClipEffect& effect : clip.effects) {
        for (const float param : effect.params) {
            if (!std::isfinite(param)) {
                return Fail(error_out,
                            "clip effect params must be finite");
            }
        }
    }
    for (const auto& other : project.clips) {
        if (other.id == exclude_clip_id || other.track != clip.track) {
            continue;
        }
        if (ClipsOverlap(clip, other)) {
            return Fail(error_out,
                        "clip overlaps another clip on the same track");
        }
    }
    return true;
}

int AddTrack(Project& project, TrackKind kind, const std::string& name) {
    project.tracks.push_back(Track{kind, name});
    // Defensive: appending an empty track cannot invalidate a
    // transition, but every mutating op re-asserts the invariant.
    DropInvalidTransitions(project);
    return static_cast<int>(project.tracks.size()) - 1;
}

bool RemoveTrack(Project& project, int track_index,
                 std::string* error_out) {
    if (track_index < 0 ||
        track_index >= static_cast<int>(project.tracks.size())) {
        return Fail(error_out, "track index out of range");
    }
    for (const auto& clip : project.clips) {
        if (clip.track == track_index) {
            return Fail(error_out, "track still contains clips");
        }
    }
    project.tracks.erase(project.tracks.begin() +
                         static_cast<std::ptrdiff_t>(track_index));
    for (auto& clip : project.clips) {
        if (clip.track > track_index) {
            --clip.track;
        }
    }
    // Reindexing preserves same-track relationships, so this is a
    // no-op for valid projects — kept for the every-mutating-op rule.
    DropInvalidTransitions(project);
    return true;
}

bool AddClip(Project& project, const Clip& clip, const MediaBounds& bounds,
             std::string* error_out) {
    if (!ValidateClipPlacement(project, clip, bounds, "", error_out)) {
        return false;
    }
    project.clips.push_back(clip);
    // Defensive: a valid add cannot break a transition (adjacency leaves
    // no gap to insert into and overlap validation forbids landing
    // inside either clip), but keep the invariant explicit.
    DropInvalidTransitions(project);
    return true;
}

bool RemoveClip(Project& project, const std::string& clip_id) {
    const auto it = std::find_if(project.clips.begin(), project.clips.end(),
                                 [&](const Clip& clip) {
                                     return clip.id == clip_id;
                                 });
    if (it == project.clips.end()) {
        return false;
    }
    project.clips.erase(it);
    // Transitions referencing the removed clip drop; survivors keep
    // their adjacency (their boundaries did not move).
    DropInvalidTransitions(project);
    return true;
}

bool MoveClip(Project& project, const std::string& clip_id,
              double new_timeline_start, const MediaBounds& bounds,
              std::string* error_out) {
    Clip* clip = FindClip(project, clip_id);
    if (clip == nullptr) {
        return Fail(error_out, "clip not found");
    }
    Clip moved = *clip;
    moved.timeline_start = new_timeline_start;
    if (!ValidateClipPlacement(project, moved, bounds, clip_id,
                               error_out)) {
        return false;
    }
    // Assign in place — preserves the clip's vector position.
    *clip = moved;
    DropInvalidTransitions(project);
    return true;
}

bool TrimClip(Project& project, const std::string& clip_id, TrimEdge edge,
              double new_point, const MediaBounds& bounds,
              std::string* error_out) {
    Clip* clip = FindClip(project, clip_id);
    if (clip == nullptr) {
        return Fail(error_out, "clip not found");
    }
    // NaN new_point would poison new_in/new_out/new_start — every
    // relational check below is false for NaN, so reject up front.
    if (!std::isfinite(new_point)) {
        return Fail(error_out, "trim point must be finite");
    }
    const double old_in = clip->in_point;
    const double old_out = old_in + clip->duration;
    double new_in = old_in;
    double new_out = old_out;
    double new_start = clip->timeline_start;
    if (edge == TrimEdge::In) {
        // New in-point: the out point stays, the timeline start slides
        // with the source offset.
        new_in = new_point;
        new_start = clip->timeline_start + (new_point - old_in);
    } else {
        // New out-point: in-point and timeline start stay.
        new_out = new_point;
    }
    const double new_duration = new_out - new_in;
    if (new_duration <= kMinClipDuration) {
        return Fail(error_out, "trim would collapse the clip");
    }
    if (new_in < 0.0) {
        return Fail(error_out, "trim in-point is before zero");
    }
    if (bounds.duration_seconds > 0.0 &&
        new_out > bounds.duration_seconds) {
        return Fail(error_out, "trim exceeds the media duration");
    }
    Clip trimmed = *clip;
    trimmed.in_point = new_in;
    trimmed.timeline_start = new_start;
    trimmed.duration = new_duration;
    if (!ValidateClipPlacement(project, trimmed, bounds, clip_id,
                               error_out)) {
        return false;
    }
    *clip = trimmed;
    DropInvalidTransitions(project);
    return true;
}

bool SplitClip(Project& project, const std::string& clip_id,
               double split_timeline_t, const std::string& new_clip_id,
               std::string* error_out) {
    Clip* clip = FindClip(project, clip_id);
    if (clip == nullptr) {
        return Fail(error_out, "clip not found");
    }
    // NaN split_timeline_t defeats the range check below (every
    // comparison is false) and would commit two NaN clips.
    if (!std::isfinite(split_timeline_t)) {
        return Fail(error_out, "split point must be finite");
    }
    const double start = clip->timeline_start;
    const double end = ClipEndSeconds(*clip);
    if (split_timeline_t <= start + kMinClipDuration ||
        split_timeline_t >= end - kMinClipDuration) {
        return Fail(error_out,
                    "split point must lie strictly inside the clip");
    }
    if (new_clip_id.empty()) {
        return Fail(error_out, "new clip id is empty");
    }
    if (FindClip(project, new_clip_id) != nullptr) {
        return Fail(error_out, "new clip id already exists");
    }

    // Build the second half BEFORE mutating the first — `clip` points
    // into project.clips and push_back may reallocate.
    Clip second = *clip;
    second.id = new_clip_id;
    second.in_point = clip->in_point + (split_timeline_t - start);
    second.timeline_start = split_timeline_t;
    second.duration = end - split_timeline_t;
    second.fade_in = 0.0;   // fade_out stays on the second half

    // Validate the split-off half before committing anything (the
    // model's validate-then-mutate contract). The source clip still
    // occupies the same span, so it is excluded from the overlap scan
    // via exclude_clip_id; both halves are strict subsets of its
    // already-validated extent. MediaBounds{} disables the
    // media-duration clamp — SplitClip has no bounds parameter and the
    // halves inherit the source clip's in/out bounds by construction.
    if (!ValidateClipPlacement(project, second, MediaBounds{}, clip_id,
                               error_out)) {
        return false;
    }

    clip->duration = split_timeline_t - start;
    clip->fade_out = 0.0;   // fade_in stays on the first half

    project.clips.push_back(second);

    // The cut moved from the old clip end to the split point: outgoing
    // transitions now leave from the second half; incoming ones keep
    // pointing at the first half (which retains clip_id).
    for (auto& transition : project.transitions) {
        if (transition.from_clip == clip_id) {
            transition.from_clip = new_clip_id;
        }
    }
    DropInvalidTransitions(project);
    return true;
}

bool ReplaceClip(Project& project, const std::string& clip_id,
                 const Clip& replacement, const MediaBounds& bounds,
                 std::string* error_out) {
    Clip* clip = FindClip(project, clip_id);
    if (clip == nullptr) {
        return Fail(error_out, "clip not found");
    }
    Clip updated = replacement;
    updated.id = clip_id;   // caller-supplied id is ignored
    if (!ValidateClipPlacement(project, updated, bounds, clip_id,
                               error_out)) {
        return false;
    }
    *clip = updated;
    DropInvalidTransitions(project);
    return true;
}

bool AddTransition(Project& project, const std::string& from_clip,
                   const std::string& to_clip, Transition::Type type,
                   double duration, std::string* error_out) {
    if (from_clip == to_clip) {
        return Fail(error_out, "transition endpoints must differ");
    }
    const Clip* from = FindClip(project, from_clip);
    const Clip* to = FindClip(project, to_clip);
    if (from == nullptr || to == nullptr) {
        return Fail(error_out, "transition endpoint clip not found");
    }
    if (from->track != to->track) {
        return Fail(error_out, "transition clips must share a track");
    }
    if (from->track < 0 ||
        from->track >= static_cast<int>(project.tracks.size()) ||
        project.tracks[static_cast<std::size_t>(from->track)].kind !=
            TrackKind::Video) {
        return Fail(error_out, "transitions require a video track");
    }
    // NaN duration defeats `<= 0` (comparison is false) and would also
    // make the adjacency check below pass vacuously — reject non-finite
    // explicitly, consistent with the positive-duration rule.
    if (!std::isfinite(duration)) {
        return Fail(error_out, "transition duration must be finite");
    }
    if (duration <= 0.0) {
        return Fail(error_out, "transition duration must be positive");
    }
    if (std::fabs(to->timeline_start - ClipEndSeconds(*from)) > kEpsilon) {
        return Fail(error_out, "transition clips must be adjacent");
    }
    for (const auto& existing : project.transitions) {
        if (existing.from_clip == from_clip &&
            existing.to_clip == to_clip) {
            return Fail(error_out, "transition already exists");
        }
    }
    project.transitions.push_back(
        Transition{from_clip, to_clip, type, duration});
    return true;
}

bool RemoveTransition(Project& project, const std::string& from_clip,
                      const std::string& to_clip) {
    const auto it = std::find_if(project.transitions.begin(),
                                 project.transitions.end(),
                                 [&](const Transition& transition) {
                                     return transition.from_clip ==
                                                from_clip &&
                                            transition.to_clip == to_clip;
                                 });
    if (it == project.transitions.end()) {
        return false;
    }
    project.transitions.erase(it);
    return true;
}

void DropInvalidTransitions(Project& project) {
    std::erase_if(project.transitions, [&](const Transition& transition) {
        return !TransitionEndpointsAdjacent(project, transition);
    });
}

std::pair<double, double> ClipTransitionHandles(
    const Project& project, const std::string& clip_id) {
    double in_handle = 0.0;
    double out_handle = 0.0;
    for (const auto& transition : project.transitions) {
        if (transition.to_clip == clip_id) {
            in_handle = std::max(in_handle, transition.duration * 0.5);
        }
        if (transition.from_clip == clip_id) {
            out_handle = std::max(out_handle, transition.duration * 0.5);
        }
    }
    return {in_handle, out_handle};
}

Transform FitTransformToCanvas(int src_w, int src_h, int canvas_w,
                               int canvas_h) {
    Transform transform;
    if (src_w <= 0 || src_h <= 0 || canvas_w <= 0 || canvas_h <= 0) {
        return transform;
    }
    const float scale = std::min(
        static_cast<float>(canvas_w) / static_cast<float>(src_w),
        static_cast<float>(canvas_h) / static_cast<float>(src_h));
    const float displayed_w = static_cast<float>(src_w) * scale;
    const float displayed_h = static_cast<float>(src_h) * scale;
    transform.scale = scale;
    transform.offset_x =
        (static_cast<float>(canvas_w) - displayed_w) /
        (2.0f * static_cast<float>(canvas_w));
    transform.offset_y =
        (static_cast<float>(canvas_h) - displayed_h) /
        (2.0f * static_cast<float>(canvas_h));
    return transform;
}

std::string NewClipId() {
    // uuid v4 from two 64-bit random_device draws: version nibble 4 in
    // the third group, variant bits 10 in the fourth, "8-4-4-4-12" hex.
    std::random_device random;
    const std::uint64_t hi =
        (static_cast<std::uint64_t>(random()) << 32) |
        static_cast<std::uint64_t>(random());
    const std::uint64_t lo =
        (static_cast<std::uint64_t>(random()) << 32) |
        static_cast<std::uint64_t>(random());
    char buffer[37];
    std::snprintf(buffer, sizeof(buffer), "%08x-%04x-%04x-%04x-%04x%08x",
                  static_cast<unsigned int>(hi >> 32),
                  static_cast<unsigned int>((hi >> 16) & 0xFFFFu),
                  static_cast<unsigned int>((hi & 0x0FFFu) | 0x4000u),
                  static_cast<unsigned int>(((lo >> 48) & 0x3FFFu) |
                                            0x8000u),
                  static_cast<unsigned int>((lo >> 32) & 0xFFFFu),
                  static_cast<unsigned int>(lo & 0xFFFFFFFFu));
    return std::string(buffer, 36);
}

}  // namespace XER::VideoEditor
