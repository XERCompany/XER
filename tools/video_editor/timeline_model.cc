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
#include <map>
#include <random>
#include <set>

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
// Tracks+clips are passed explicitly so stored Sequences reuse the
// same predicate (their timelines obey the same invariants).
const Clip* FindClipIn(const std::vector<Clip>& clips,
                       const std::string& clip_id) {
    for (const auto& clip : clips) {
        if (clip.id == clip_id) {
            return &clip;
        }
    }
    return nullptr;
}

bool TransitionEndpointsAdjacent(const std::vector<Track>& tracks,
                                 const std::vector<Clip>& clips,
                                 const Transition& transition) {
    const Clip* from = FindClipIn(clips, transition.from_clip);
    const Clip* to = FindClipIn(clips, transition.to_clip);
    if (from == nullptr || to == nullptr) {
        return false;
    }
    if (from->track != to->track) {
        return false;
    }
    if (from->track < 0 ||
        from->track >= static_cast<int>(tracks.size())) {
        return false;
    }
    if (tracks[static_cast<std::size_t>(from->track)].kind !=
        TrackKind::Video) {
        return false;
    }
    return std::fabs(to->timeline_start - ClipEndSeconds(*from)) <=
           kEpsilon;
}

// Locked tracks reject every clip-affecting edit.
bool IsTrackLocked(const Project& project, int track_index) {
    return track_index >= 0 &&
           track_index < static_cast<int>(project.tracks.size()) &&
           project.tracks[static_cast<std::size_t>(track_index)].locked;
}

// Removes the [t0, t1) portion of every clip on ONE track: clips
// spanning a boundary are split there first (SplitClip keeps the id on
// the FIRST piece, so a clip reaching past t1 loses its id to the
// removed inside piece and the surviving tail gets a fresh one), then
// every piece still strictly intersecting the range is erased — that
// also sweeps up sub-kMinClipDuration slivers too thin to split.
// Shared by LiftRange (all unlocked tracks) and OverwriteClipAt (the
// target track). The caller stages a Project copy, so a mid-op failure
// simply discards it. Assumes the track is unlocked and t0 < t1.
bool LiftRangeOnTrack(Project& project, int track_index, double t0,
                      double t1, std::string* error_out) {
    const auto spans = [&](double t, const Clip& clip) {
        return clip.track == track_index &&
               t - clip.timeline_start > kMinClipDuration &&
               ClipEndSeconds(clip) - t > kMinClipDuration;
    };
    // Split at t0 first: the right piece may still span t1 and is cut
    // again by the second pass. Ids are collected before splitting —
    // push_back may reallocate project.clips.
    std::vector<std::string> cut_ids;
    for (const auto& clip : project.clips) {
        if (spans(t0, clip)) {
            cut_ids.push_back(clip.id);
        }
    }
    for (const auto& id : cut_ids) {
        if (!SplitClip(project, id, t0, NewClipId(), error_out)) {
            return false;
        }
    }
    cut_ids.clear();
    for (const auto& clip : project.clips) {
        if (spans(t1, clip)) {
            cut_ids.push_back(clip.id);
        }
    }
    for (const auto& id : cut_ids) {
        if (!SplitClip(project, id, t1, NewClipId(), error_out)) {
            return false;
        }
    }
    std::erase_if(project.clips, [&](const Clip& clip) {
        return clip.track == track_index && clip.timeline_start < t1 &&
               ClipEndSeconds(clip) > t0;
    });
    return true;
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
    for (const Marker& marker : project.markers) {
        points.push_back(marker.time);
    }
    if (project.work_out > project.work_in) {
        points.push_back(project.work_in);
        points.push_back(project.work_out);
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
    if (!std::isfinite(clip.playback_rate) ||
        clip.playback_rate <= 0.0) {
        return Fail(error_out, "clip playback rate must be positive");
    }
    if (!std::isfinite(clip.pan) || clip.pan < -1.0f ||
        clip.pan > 1.0f) {
        return Fail(error_out, "clip pan must be within [-1, 1]");
    }
    // Finite fields can still overflow to ±inf when summed — reject
    // extents that are not representable for the same reason.
    if (!std::isfinite(clip.in_point + clip.duration) ||
        !std::isfinite(ClipEndSeconds(clip))) {
        return Fail(error_out, "clip extent is not representable");
    }
    // playback_rate stretches the SOURCE span consumed: the media
    // bound applies to in_point + duration * rate, not duration alone.
    // For compound clips the "media" is the referenced sequence —
    // callers pass its duration through the same MediaBounds field.
    if (bounds.duration_seconds > 0.0 &&
        clip.in_point + clip.duration * clip.playback_rate >
            bounds.duration_seconds + kEpsilon) {
        return Fail(error_out, "clip exceeds the media duration");
    }
    // Compound clips must reference a stored sequence, and an
    // audio-lane compound may only carry audio content — same
    // content-kind rule as audio-only media.
    if (!clip.compound.empty()) {
        const Sequence* compound_seq =
            FindSequence(project, clip.compound);
        if (compound_seq == nullptr) {
            return Fail(error_out,
                        "compound clip references a missing sequence");
        }
        if (project.tracks[static_cast<std::size_t>(clip.track)].kind ==
            TrackKind::Audio) {
            for (const Clip& inner : compound_seq->clips) {
                if (inner.track >= 0 &&
                    inner.track < static_cast<int>(
                                      compound_seq->tracks.size()) &&
                    compound_seq->tracks[static_cast<std::size_t>(
                                             inner.track)]
                            .kind == TrackKind::Video) {
                    return Fail(error_out,
                                "audio-track compound references a "
                                "sequence with video content");
                }
            }
        }
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
    for (const ClipMask& mask : clip.masks) {
        if (!std::isfinite(mask.cx) || !std::isfinite(mask.cy) ||
            !std::isfinite(mask.w) || !std::isfinite(mask.h) ||
            !std::isfinite(mask.feather) || mask.w <= 0.0f ||
            mask.h <= 0.0f || mask.feather < 0.0f) {
            return Fail(error_out, "clip mask geometry is invalid");
        }
    }
    for (const ReframeSample& sample : clip.reframe.samples) {
        if (!std::isfinite(sample.t) || !std::isfinite(sample.cx) ||
            !std::isfinite(sample.cy) || !std::isfinite(sample.w) ||
            !std::isfinite(sample.h) ||
            !std::isfinite(sample.confidence)) {
            return Fail(error_out,
                        "clip smart-reframe samples must be finite");
        }
    }
    for (const ReframeSample& sample : clip.object_track.samples) {
        if (!std::isfinite(sample.t) || !std::isfinite(sample.cx) ||
            !std::isfinite(sample.cy) || !std::isfinite(sample.w) ||
            !std::isfinite(sample.h) ||
            !std::isfinite(sample.confidence)) {
            return Fail(error_out,
                        "clip object-track samples must be finite");
        }
    }
    for (const auto& entry : clip.keyframes) {
        double prev_time = -std::numeric_limits<double>::infinity();
        for (const Keyframe& key : entry.second) {
            if (!std::isfinite(key.time) ||
                !std::isfinite(key.value) || key.time < 0.0 ||
                key.time > clip.duration + kEpsilon ||
                key.time < prev_time - kEpsilon) {
                return Fail(error_out,
                            "clip keyframes must be finite, sorted "
                            "by time and inside [0, duration]");
            }
            prev_time = key.time;
        }
    }
    if (IsTrackLocked(project, clip.track)) {
        return Fail(error_out, "track is locked");
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
    if (IsTrackLocked(project, it->track)) {
        return false;
    }
    project.clips.erase(it);
    // Transitions referencing the removed clip drop; survivors keep
    // their adjacency (their boundaries did not move).
    DropInvalidTransitions(project);
    return true;
}

bool RippleDeleteClip(Project& project, const std::string& clip_id,
                      std::string* error_out) {
    const Clip* clip = FindClip(project, clip_id);
    if (clip == nullptr) {
        return Fail(error_out, "clip not found");
    }
    if (IsTrackLocked(project, clip->track)) {
        return Fail(error_out, "track is locked");
    }
    // Read the position fields BEFORE erasing — `clip` points into
    // project.clips.
    const int track = clip->track;
    const double duration = clip->duration;
    const double end = ClipEndSeconds(*clip);
    project.clips.erase(
        std::remove_if(project.clips.begin(), project.clips.end(),
                       [&](const Clip& c) { return c.id == clip_id; }),
        project.clips.end());
    // Close the gap: every clip on the same track starting at/after
    // the removed clip's end slides left by its duration. Clips before
    // it and clips on other tracks keep their position.
    for (auto& other : project.clips) {
        if (other.track == track &&
            other.timeline_start >= end - kEpsilon) {
            other.timeline_start =
                std::max(0.0, other.timeline_start - duration);
        }
    }
    DropInvalidTransitions(project);
    return true;
}

bool LiftRange(Project& project, double t0, double t1, bool close_gap,
               std::string* error_out,
               const std::vector<bool>& track_target) {
    if (!std::isfinite(t0) || !std::isfinite(t1)) {
        return Fail(error_out, "lift range must be finite");
    }
    if (t0 < 0.0) {
        return Fail(error_out,
                    "lift range must start at or after zero");
    }
    if (t1 <= t0) {
        return Fail(error_out, "lift range must be non-empty");
    }
    if (!track_target.empty() &&
        track_target.size() != project.tracks.size()) {
        return Fail(error_out,
                    "lift target mask must match the track count");
    }
    // Multi-track splits/removals are one transaction: everything runs
    // on a staged copy and is committed only when every track's edit
    // succeeded.
    Project staged = project;
    for (std::size_t t = 0; t < staged.tracks.size(); ++t) {
        const int track = static_cast<int>(t);
        // Locked tracks are skipped silently (a lift leaves them
        // untouched; it is not an error to have locked tracks).
        // Untargeted tracks (the destination/auto-select mask) skip
        // the same way — Premiere lift/extract only reads targets.
        if (IsTrackLocked(staged, track) ||
            (!track_target.empty() && !track_target[t])) {
            continue;
        }
        if (!LiftRangeOnTrack(staged, track, t0, t1, error_out)) {
            return false;
        }
        if (close_gap) {
            // Extract: followers on this track close the hole left
            // behind. A clip starting exactly at t1 lands exactly at
            // t0 — flush against the remaining left edge.
            const double shift = t1 - t0;
            for (auto& clip : staged.clips) {
                if (clip.track == track &&
                    clip.timeline_start >= t1 - kEpsilon) {
                    clip.timeline_start =
                        std::max(0.0, clip.timeline_start - shift);
                }
            }
        }
    }
    DropInvalidTransitions(staged);
    project = std::move(staged);
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

bool RollingEdit(Project& project, const std::string& from_clip,
                 const std::string& to_clip, double delta_seconds,
                 const MediaBounds& from_bounds,
                 const MediaBounds& to_bounds, std::string* error_out) {
    if (!std::isfinite(delta_seconds)) {
        return Fail(error_out, "rolling delta must be finite");
    }
    const Clip* from = FindClip(project, from_clip);
    const Clip* to = FindClip(project, to_clip);
    if (from == nullptr || to == nullptr) {
        return Fail(error_out, "clip not found");
    }
    if (from_clip == to_clip) {
        return Fail(error_out,
                    "rolling edit needs two distinct clips");
    }
    if (from->track != to->track) {
        return Fail(error_out,
                    "rolling edit clips must share a track");
    }
    if (IsTrackLocked(project, from->track)) {
        return Fail(error_out, "track is locked");
    }
    // The pair must meet at a cut: `to` starts where `from` ends.
    if (std::fabs(to->timeline_start - ClipEndSeconds(*from)) >
        kEpsilon) {
        return Fail(error_out, "rolling edit requires adjacent clips");
    }
    // delta > 0 moves the cut right by delta TIMELINE seconds: `from`
    // grows out of its out-handle while `to` gives up head material —
    // each source-side point moves by delta * its own playback_rate.
    // Staged copy + ordered trims keep it transactional: for delta > 0
    // `to` vacates space first, for delta < 0 `from` does, so the
    // growing side is always validated against the already-shrunk
    // neighbour (validating `from` against the unmodified project
    // would spuriously reject it for overlapping the old `to`).
    Project staged = project;
    const double from_out =
        from->in_point + from->duration * from->playback_rate;
    const double to_in =
        to->in_point + delta_seconds * to->playback_rate;
    const double from_new_out =
        from_out + delta_seconds * from->playback_rate;
    if (delta_seconds >= 0.0) {
        if (!TrimClip(staged, to_clip, TrimEdge::In, to_in, to_bounds,
                      error_out) ||
            !TrimClip(staged, from_clip, TrimEdge::Out, from_new_out,
                      from_bounds, error_out)) {
            return false;
        }
    } else {
        if (!TrimClip(staged, from_clip, TrimEdge::Out, from_new_out,
                      from_bounds, error_out) ||
            !TrimClip(staged, to_clip, TrimEdge::In, to_in, to_bounds,
                      error_out)) {
            return false;
        }
    }
    project = std::move(staged);   // TrimClip dropped dead transitions
    return true;
}

bool SlipClip(Project& project, const std::string& clip_id,
              double delta_seconds, const MediaBounds& bounds,
              std::string* error_out) {
    if (!std::isfinite(delta_seconds)) {
        return Fail(error_out, "slip delta must be finite");
    }
    Clip* clip = FindClip(project, clip_id);
    if (clip == nullptr) {
        return Fail(error_out, "clip not found");
    }
    Clip slipped = *clip;
    slipped.in_point += delta_seconds;
    // Placement validation enforces the source-window bounds: in_point
    // >= 0 and in_point + duration * rate <= media duration (when
    // known). Timeline geometry is untouched, so a valid slip is just
    // an in-point swap.
    if (!ValidateClipPlacement(project, slipped, bounds, clip_id,
                               error_out)) {
        return false;
    }
    *clip = slipped;
    // The timeline did not change — no transition can break — but keep
    // the every-mutating-op invariant explicit.
    DropInvalidTransitions(project);
    return true;
}

bool SlideClip(Project& project, const std::string& clip_id,
               double delta_seconds, const MediaBounds& bounds,
               std::string* error_out) {
    if (!std::isfinite(delta_seconds)) {
        return Fail(error_out, "slide delta must be finite");
    }
    const Clip* clip = FindClip(project, clip_id);
    if (clip == nullptr) {
        return Fail(error_out, "clip not found");
    }
    if (IsTrackLocked(project, clip->track)) {
        return Fail(error_out, "track is locked");
    }
    const double start = clip->timeline_start;
    const double end = ClipEndSeconds(*clip);
    // Immediate neighbours on the same track — the only clips that can
    // absorb the slide. A neighbour exactly adjacent (within kEpsilon)
    // stretches/shrinks into the vacated span; a gapped one merely
    // bounds the move, i.e. the clip behaves like MoveClip on that
    // side (Premiere slide semantics). The neighbours' own media
    // bounds are unknown to this op — their out-growth is bounded only
    // by the no-overlap invariants, head-growth by in_point >= 0.
    const Clip* prev = nullptr;   // latest clip ending at/before start
    const Clip* next = nullptr;   // earliest clip starting at/after end
    for (const auto& other : project.clips) {
        if (other.track != clip->track || other.id == clip_id) {
            continue;
        }
        if (ClipEndSeconds(other) <= start + kEpsilon &&
            (prev == nullptr ||
             ClipEndSeconds(other) > ClipEndSeconds(*prev))) {
            prev = &other;
        }
        if (other.timeline_start >= end - kEpsilon &&
            (next == nullptr ||
             other.timeline_start < next->timeline_start)) {
            next = &other;
        }
    }
    const bool prev_adjacent =
        prev != nullptr &&
        std::fabs(ClipEndSeconds(*prev) - start) <= kEpsilon;
    const bool next_adjacent =
        next != nullptr &&
        std::fabs(next->timeline_start - end) <= kEpsilon;
    const std::string prev_id = prev != nullptr ? prev->id : "";
    const std::string next_id = next != nullptr ? next->id : "";

    Clip moved = *clip;
    moved.timeline_start = start + delta_seconds;
    Clip new_prev = prev != nullptr ? *prev : Clip{};
    Clip new_next = next != nullptr ? *next : Clip{};
    if (delta_seconds >= 0.0) {
        // Slide right: prev's out-point consumes the vacated head,
        // next's head trims away the covered tail.
        if (prev_adjacent) {
            new_prev.duration += delta_seconds;
        }
        if (next_adjacent) {
            new_next.in_point += delta_seconds * next->playback_rate;
            new_next.timeline_start += delta_seconds;
            new_next.duration -= delta_seconds;
        }
    } else {
        // Slide left: prev shrinks back, next extends into the gap.
        const double back = -delta_seconds;
        if (prev_adjacent) {
            new_prev.duration -= back;
        }
        if (next_adjacent) {
            new_next.in_point -= back * next->playback_rate;
            new_next.timeline_start -= back;
            new_next.duration += back;
        }
    }
    // All three clips are committed to a staged copy and validated
    // against each other there — validating against the unmodified
    // project would compare the moved clip against the neighbours' OLD
    // positions. A neighbour that cannot absorb (collapsed duration,
    // in-point before zero, overlap) rejects the whole edit.
    Project staged = project;
    *FindClip(staged, clip_id) = moved;
    if (prev != nullptr) {
        *FindClip(staged, prev_id) = new_prev;
    }
    if (next != nullptr) {
        *FindClip(staged, next_id) = new_next;
    }
    if (!ValidateClipPlacement(staged, moved, bounds, clip_id,
                               error_out) ||
        (prev != nullptr &&
         !ValidateClipPlacement(staged, new_prev, MediaBounds{},
                                prev_id, error_out)) ||
        (next != nullptr &&
         !ValidateClipPlacement(staged, new_next, MediaBounds{},
                                next_id, error_out))) {
        return false;
    }
    DropInvalidTransitions(staged);
    project = std::move(staged);
    return true;
}

bool SetClipRate(Project& project, const std::string& clip_id,
                 double rate, const MediaBounds& bounds,
                 std::string* error_out) {
    if (!std::isfinite(rate) || rate <= 0.0) {
        return Fail(error_out,
                    "playback rate must be finite and positive");
    }
    Clip* clip = FindClip(project, clip_id);
    if (clip == nullptr) {
        return Fail(error_out, "clip not found");
    }
    // Premiere Speed/Duration: the consumed SOURCE span is preserved —
    // in_point and in_point + duration*rate stay fixed while the
    // timeline duration contracts/expands to consumed/rate.
    Clip updated = *clip;
    const double consumed = clip->duration * clip->playback_rate;
    updated.playback_rate = rate;
    updated.duration = consumed / rate;
    // Re-validates everything: duration floor, media bound, keyframes
    // still inside [0, duration], and the overlap invariant against
    // the clip's new extent (growth into a neighbour is rejected
    // whole).
    if (!ValidateClipPlacement(project, updated, bounds, clip_id,
                               error_out)) {
        return false;
    }
    *clip = updated;
    DropInvalidTransitions(project);
    return true;
}

bool InsertClipAt(Project& project, const Clip& clip,
                  const MediaBounds& bounds, std::string* error_out) {
    if (clip.track < 0 ||
        clip.track >= static_cast<int>(project.tracks.size())) {
        return Fail(error_out, "clip track index out of range");
    }
    if (IsTrackLocked(project, clip.track)) {
        return Fail(error_out, "track is locked");
    }
    // Stage the ripple first: every clip at/after the insert point
    // shifts right by the inserted duration, then the insert is
    // validated against the shifted layout — an invalid clip, or an
    // existing clip SPANNING the insert point (it cannot shift and
    // would overlap), discards the whole edit.
    Project staged = project;
    for (auto& other : staged.clips) {
        if (other.track == clip.track &&
            other.timeline_start >= clip.timeline_start - kEpsilon) {
            other.timeline_start += clip.duration;
        }
    }
    if (!ValidateClipPlacement(staged, clip, bounds, "", error_out)) {
        return false;
    }
    staged.clips.push_back(clip);
    DropInvalidTransitions(staged);
    project = std::move(staged);
    return true;
}

bool OverwriteClipAt(Project& project, const Clip& clip,
                     const MediaBounds& bounds, std::string* error_out) {
    if (clip.track < 0 ||
        clip.track >= static_cast<int>(project.tracks.size())) {
        return Fail(error_out, "clip track index out of range");
    }
    if (IsTrackLocked(project, clip.track)) {
        return Fail(error_out, "track is locked");
    }
    // Clear [start, start+duration) on a staged copy — the same
    // boundary-split rules as LiftRange — then validate and add the
    // new clip into the cleared span.
    Project staged = project;
    const double t0 = clip.timeline_start;
    const double t1 = t0 + clip.duration;
    if (!LiftRangeOnTrack(staged, clip.track, t0, t1, error_out)) {
        return false;
    }
    if (!ValidateClipPlacement(staged, clip, bounds, "", error_out)) {
        return false;
    }
    staged.clips.push_back(clip);
    DropInvalidTransitions(staged);
    project = std::move(staged);
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
    const double old_out = old_in + clip->duration * clip->playback_rate;
    double new_in = old_in;
    double new_out = old_out;
    double new_start = clip->timeline_start;
    if (edge == TrimEdge::In) {
        // New in-point: the out point stays, the timeline start slides
        // with the source offset (scaled by playback_rate).
        new_in = new_point;
        new_start =
            clip->timeline_start + (new_point - old_in) / clip->playback_rate;
    } else {
        // New out-point: in-point and timeline start stay.
        new_out = new_point;
    }
    const double new_duration = (new_out - new_in) / clip->playback_rate;
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
    // Timeline offset maps to source offset scaled by playback_rate.
    second.in_point =
        clip->in_point + (split_timeline_t - start) * clip->playback_rate;
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
                   double duration, std::string* error_out,
                   const std::string& script, const std::string& xfade,
                   const std::string& xer_fx,
                   const std::string& xer_params) {
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
    if (IsTrackLocked(project, from->track)) {
        return Fail(error_out, "track is locked");
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
        Transition{from_clip, to_clip, type, duration, script, xfade,
                   xer_fx, xer_params});
    return true;
}

const std::vector<Transition::Type>& AllTransitionTypes() {
    static const std::vector<Transition::Type> kAll = {
        Transition::Type::Crossfade,
        Transition::Type::WipeLeft,   Transition::Type::WipeRight,
        Transition::Type::WipeUp,     Transition::Type::WipeDown,
        Transition::Type::SlideLeft,  Transition::Type::SlideRight,
        Transition::Type::SlideUp,    Transition::Type::SlideDown,
        Transition::Type::DipToBlack, Transition::Type::DipToWhite,
        Transition::Type::AdditiveDissolve,
        Transition::Type::Xfade,
        Transition::Type::XerFx,
        Transition::Type::Script,
    };
    return kAll;
}

const char* TransitionTypeLabel(Transition::Type type) {
    switch (type) {
        case Transition::Type::Crossfade:
            return "Crossfade";
        case Transition::Type::WipeLeft:
            return "Wipe Left";
        case Transition::Type::WipeRight:
            return "Wipe Right";
        case Transition::Type::WipeUp:
            return "Wipe Up";
        case Transition::Type::WipeDown:
            return "Wipe Down";
        case Transition::Type::SlideLeft:
            return "Slide Left";
        case Transition::Type::SlideRight:
            return "Slide Right";
        case Transition::Type::SlideUp:
            return "Slide Up";
        case Transition::Type::SlideDown:
            return "Slide Down";
        case Transition::Type::DipToBlack:
            return "Dip to Black";
        case Transition::Type::DipToWhite:
            return "Dip to White";
        case Transition::Type::AdditiveDissolve:
            return "Additive Dissolve";
        case Transition::Type::Xfade:
            return "XFade";
        case Transition::Type::XerFx:
            return "XER";
        case Transition::Type::Script:
            return "Script";
    }
    return "Crossfade";  // unreachable
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
    const Clip* from = FindClip(project, from_clip);
    if (from != nullptr && IsTrackLocked(project, from->track)) {
        return false;
    }
    project.transitions.erase(it);
    return true;
}

bool SetTransitionDuration(Project& project, const std::string& from_clip,
                           const std::string& to_clip, double duration,
                           std::string* error_out) {
    for (auto& transition : project.transitions) {
        if (transition.from_clip != from_clip ||
            transition.to_clip != to_clip) {
            continue;
        }
        const Clip* from = FindClip(project, from_clip);
        if (from != nullptr && IsTrackLocked(project, from->track)) {
            return Fail(error_out, "track is locked");
        }
        if (!std::isfinite(duration)) {
            return Fail(error_out, "transition duration must be finite");
        }
        if (duration <= 0.0) {
            return Fail(error_out,
                        "transition duration must be positive");
        }
        transition.duration = duration;
        return true;
    }
    return Fail(error_out, "transition not found");
}

double TransitionDurationLimit(const Project& project,
                               const Transition& transition,
                               const MediaBounds& from_bounds,
                               const MediaBounds& to_bounds) {
    const Clip* from = FindClip(project, transition.from_clip);
    const Clip* to = FindClip(project, transition.to_clip);
    if (from == nullptr || to == nullptr) {
        return 0.0;
    }
    double limit = std::numeric_limits<double>::infinity();
    // The centered window may not reach past either endpoint's own
    // span — a wider half would overlap a third clip's contribution
    // window on the track.
    limit = std::min(limit, 2.0 * std::min(from->duration, to->duration));
    // Media-source handles: the window reads (duration/2) timeline
    // seconds of source beyond each clip's consumed span — scaled by
    // playback_rate on the source side. Generated sources never read
    // source (time-invariant), so their side is unbounded.
    if (from->source.kind == SourceKind::Media &&
        from_bounds.duration_seconds > 0.0 &&
        from->playback_rate > 0.0) {
        const double tail = from_bounds.duration_seconds -
            (from->in_point + from->duration * from->playback_rate);
        limit = std::min(
            limit, std::max(0.0, 2.0 * tail / from->playback_rate));
    }
    if (to->source.kind == SourceKind::Media &&
        to->playback_rate > 0.0) {
        limit = std::min(
            limit, std::max(0.0, 2.0 * to->in_point / to->playback_rate));
    }
    return limit;
}

void DropInvalidTransitions(Project& project) {
    std::erase_if(project.transitions, [&](const Transition& transition) {
        return !TransitionEndpointsAdjacent(project.tracks,
                                            project.clips, transition);
    });
}

// Same prune for a stored sequence's transitions — identical validity
// rules over sequence-local tracks/clips.
void DropInvalidTransitions(Sequence& sequence) {
    std::erase_if(sequence.transitions, [&](const Transition& transition) {
        return !TransitionEndpointsAdjacent(sequence.tracks,
                                            sequence.clips, transition);
    });
}

double Sequence::duration() const {
    double end = 0.0;
    for (const auto& clip : clips) {
        end = std::max(end, ClipEndSeconds(clip));
    }
    return end;
}

const Sequence* FindSequence(const Project& project,
                             const std::string& sequence_id) {
    for (const auto& sequence : project.sequences) {
        if (sequence.id == sequence_id) {
            return &sequence;
        }
    }
    return nullptr;
}

Sequence* FindSequence(Project& project, const std::string& sequence_id) {
    for (auto& sequence : project.sequences) {
        if (sequence.id == sequence_id) {
            return &sequence;
        }
    }
    return nullptr;
}

bool IsCompoundClip(const Clip& clip) { return !clip.compound.empty(); }

bool SequenceInUse(const Project& project,
                   const std::string& sequence_id) {
    const auto refs = [&](const std::vector<Clip>& clips) {
        for (const Clip& clip : clips) {
            if (clip.compound == sequence_id) {
                return true;
            }
        }
        return false;
    };
    if (refs(project.clips)) {
        return true;
    }
    for (const Sequence& sequence : project.sequences) {
        if (refs(sequence.clips)) {
            return true;
        }
    }
    return false;
}

Project SequenceProject(const Project& owner, const Sequence& sequence) {
    Project out = owner;   // canvas, fps, proxy fields, sequence store
    out.tracks = sequence.tracks;
    out.clips = sequence.clips;
    out.transitions = sequence.transitions;
    out.markers = sequence.markers;
    out.work_in = sequence.work_in;
    out.work_out = sequence.work_out;
    // Inner holes composite through to whatever sits below the
    // compound clip — a transparent backdrop, not the project bg.
    out.bg_color = {0, 0, 0, 0};
    // The inner timeline has no master fader of its own — the owning
    // project's fader applies once at its own level.
    out.master_gain_db = 0.0f;
    return out;
}

bool CompoundGraphHasCycle(const Project& project) {
    // DFS over the sequence-reference graph. States: white (unseen),
    // gray (on the current path), black (done). A gray revisit is a
    // cycle. The graph is small (one entry per stored sequence), so
    // plain recursion with set lookups is fine — and the depth cap
    // keeps a hostile file from overflowing the stack.
    enum class Mark { kWhite, kGray, kBlack };
    std::map<std::string, Mark> marks;
    const auto visit = [&](auto&& self, const Sequence& sequence,
                           int depth) -> bool {
        if (depth > kMaxCompoundDepth) {
            return false;   // treat runaway depth as malformed
        }
        Mark& mark = marks[sequence.id];
        if (mark == Mark::kGray) {
            return false;   // back edge — cycle
        }
        if (mark == Mark::kBlack) {
            return true;
        }
        mark = Mark::kGray;
        for (const Clip& clip : sequence.clips) {
            if (clip.compound.empty()) {
                continue;
            }
            const Sequence* inner = FindSequence(project, clip.compound);
            if (inner == nullptr) {
                continue;   // dangling ref — reported elsewhere
            }
            if (!self(self, *inner, depth + 1)) {
                return false;
            }
        }
        mark = Mark::kBlack;
        return true;
    };
    // Reach from every entry point — a self-contained cycle among
    // unreferenced sequences is still malformed.
    auto check_clips = [&](const std::vector<Clip>& clips) -> bool {
        for (const Clip& clip : clips) {
            if (clip.compound.empty()) {
                continue;
            }
            const Sequence* target = FindSequence(project, clip.compound);
            if (target != nullptr && !visit(visit, *target, 1)) {
                return false;
            }
        }
        return true;
    };
    if (!check_clips(project.clips)) {
        return false;
    }
    for (const Sequence& sequence : project.sequences) {
        if (!check_clips(sequence.clips)) {
            return false;
        }
    }
    return true;
}

bool MakeCompound(Project& project,
                  const std::vector<std::string>& clip_ids,
                  const std::string& name,
                  const std::string& new_clip_id,
                  const std::string& new_sequence_id,
                  std::string* error_out) {
    // ---- validate --------------------------------------------------
    if (clip_ids.empty()) {
        return Fail(error_out, "compound needs at least one clip");
    }
    if (new_clip_id.empty() || new_sequence_id.empty()) {
        return Fail(error_out, "compound ids must be non-empty");
    }
    if (FindClip(project, new_clip_id) != nullptr) {
        return Fail(error_out, "compound clip id already exists");
    }
    if (FindSequence(project, new_sequence_id) != nullptr) {
        return Fail(error_out, "compound sequence id already exists");
    }
    std::vector<const Clip*> selected;
    selected.reserve(clip_ids.size());
    for (const std::string& id : clip_ids) {
        const Clip* clip = FindClip(project, id);
        if (clip == nullptr) {
            return Fail(error_out, "compound selection has unknown clip");
        }
        if (clip->id == new_clip_id) {
            return Fail(error_out, "compound clip id collides");
        }
        if (std::find_if(selected.begin(), selected.end(),
                         [&](const Clip* c) { return c->id == id; }) !=
            selected.end()) {
            return Fail(error_out, "compound selection has duplicate id");
        }
        const Track* track = FindTrack(project, clip->track);
        if (track == nullptr) {
            return Fail(error_out, "compound clip on missing track");
        }
        if (track->locked) {
            return Fail(error_out, "compound source track is locked");
        }
        selected.push_back(clip);
    }
    // Distinct source tracks in index order -> inner track order.
    std::vector<int> source_tracks;
    for (const Clip* clip : selected) {
        if (std::find(source_tracks.begin(), source_tracks.end(),
                      clip->track) == source_tracks.end()) {
            source_tracks.push_back(clip->track);
        }
    }
    std::sort(source_tracks.begin(), source_tracks.end());
    bool has_video = false;
    for (const int index : source_tracks) {
        if (project.tracks[static_cast<std::size_t>(index)].kind ==
            TrackKind::Video) {
            has_video = true;
            break;
        }
    }
    // Target lane: lowest-indexed source track of the content kind —
    // a compound carrying video must sit on a video track (the model's
    // audio-only-on-audio-track invariant, generalized).
    const TrackKind content_kind =
        has_video ? TrackKind::Video : TrackKind::Audio;
    int target_track = -1;
    for (const int index : source_tracks) {
        if (project.tracks[static_cast<std::size_t>(index)].kind ==
            content_kind) {
            target_track = index;
            break;
        }
    }
    if (target_track < 0 ||
        project.tracks[static_cast<std::size_t>(target_track)].locked) {
        return Fail(error_out, "compound target track is locked");
    }

    // Span = union of selected clip bounds.
    double span_start = std::numeric_limits<double>::infinity();
    double span_end = 0.0;
    for (const Clip* clip : selected) {
        span_start = std::min(span_start, clip->timeline_start);
        span_end = std::max(span_end, ClipEndSeconds(*clip));
    }
    if (!(span_end - span_start > kMinClipDuration)) {
        return Fail(error_out, "compound span is empty");
    }
    // The compound clip must not cover a non-selected clip on its
    // target lane — the hole it would punch is unrecoverable.
    for (const Clip& clip : project.clips) {
        if (clip.track != target_track) {
            continue;
        }
        const bool chosen =
            std::find_if(selected.begin(), selected.end(),
                         [&](const Clip* c) { return c->id == clip.id; }) !=
            selected.end();
        if (!chosen && clip.timeline_start < span_end &&
            ClipEndSeconds(clip) > span_start) {
            return Fail(error_out,
                        "compound span overlaps another clip");
        }
    }

    // ---- build (pure from here on — validate-then-mutate) ----------
    std::map<int, int> track_map;   // outer index -> inner index
    Sequence sequence;
    sequence.id = new_sequence_id;
    sequence.name = name.empty() ? "Compound" : name;
    for (std::size_t i = 0; i < source_tracks.size(); ++i) {
        track_map[source_tracks[i]] = static_cast<int>(i);
        sequence.tracks.push_back(
            project.tracks[static_cast<std::size_t>(source_tracks[i])]);
        sequence.source_tracks.push_back(source_tracks[i]);
    }
    std::vector<std::string> chosen_ids;
    chosen_ids.reserve(selected.size());
    for (const Clip* clip : selected) {
        chosen_ids.push_back(clip->id);
        Clip inner = *clip;
        inner.track = track_map[clip->track];
        inner.timeline_start = clip->timeline_start - span_start;
        sequence.clips.push_back(std::move(inner));
    }
    // Transitions fully inside the selection move with their clips;
    // boundary-crossing ones die with their outer endpoint below.
    const auto chosen = [&](const std::string& id) {
        return std::find(chosen_ids.begin(), chosen_ids.end(), id) !=
               chosen_ids.end();
    };
    for (const Transition& transition : project.transitions) {
        if (chosen(transition.from_clip) && chosen(transition.to_clip)) {
            sequence.transitions.push_back(transition);
        }
    }
    DropInvalidTransitions(sequence);

    std::erase_if(project.clips, [&](const Clip& clip) {
        return chosen(clip.id);
    });

    Clip compound;
    compound.id = new_clip_id;
    compound.name = sequence.name;
    compound.track = target_track;
    compound.timeline_start = span_start;
    compound.duration = span_end - span_start;
    compound.in_point = 0.0;
    compound.compound = new_sequence_id;
    project.clips.push_back(std::move(compound));
    project.sequences.push_back(std::move(sequence));
    // Removing the selected clips invalidates their transitions and
    // any transition that touched the selection boundary.
    DropInvalidTransitions(project);
    return true;
}

bool FlattenCompound(Project& project, const std::string& clip_id,
                     std::string* error_out) {
    const Clip* clip = FindClip(project, clip_id);
    if (clip == nullptr) {
        return Fail(error_out, "unknown clip");
    }
    if (!IsCompoundClip(*clip)) {
        return Fail(error_out, "clip is not a compound clip");
    }
    const Sequence* sequence = FindSequence(project, clip->compound);
    if (sequence == nullptr) {
        return Fail(error_out, "compound clip references a missing "
                               "sequence");
    }
    const Track* host = FindTrack(project, clip->track);
    if (host == nullptr || host->locked) {
        return Fail(error_out, "compound clip track is locked");
    }
    const double rate =
        std::isfinite(clip->playback_rate) && clip->playback_rate > 0.0
            ? clip->playback_rate
            : 1.0;

    // ---- inner track -> outer track assignment ---------------------
    // Preference order per inner track (kind must match):
    //   1. its recorded source_tracks index (the lane it came from)
    //   2. the compound clip's own lane
    //   3. the next free existing lane of that kind
    //   4. a new lane appended to the project
    // "Free" means no already-assigned inner track claims it AND no
    // non-compound clip would overlap a restored clip on it — checked
    // per inner clip against the placement window.
    const auto land_window = [&](const Clip& inner) {
        // WYSIWYG restore: the inner clip lands where the compound
        // DISPLAYED it — timeline offset and duration are mapped
        // through the compound's in_point/playback_rate so a trimmed
        // or retimed compound restores what was visible.
        const double start = clip->timeline_start +
                             (inner.timeline_start - clip->in_point) /
                                 rate;
        const double duration = inner.duration / rate;
        return std::pair{start, start + duration};
    };
    const auto overlaps_outer = [&](int track, double s, double e) {
        for (const Clip& other : project.clips) {
            if (other.id == clip->id || other.track != track) {
                continue;
            }
            if (other.timeline_start < e && ClipEndSeconds(other) > s) {
                return true;
            }
        }
        return false;
    };
    const int inner_count = static_cast<int>(sequence->tracks.size());
    std::vector<int> assign(static_cast<std::size_t>(inner_count), -1);
    std::vector<bool> used(project.tracks.size(), false);
    for (int i = 0; i < inner_count; ++i) {
        const TrackKind kind = sequence->tracks[static_cast<std::size_t>(i)].kind;
        // Candidate outer indices in preference order.
        std::vector<int> candidates;
        if (i < static_cast<int>(sequence->source_tracks.size())) {
            const int src =
                sequence->source_tracks[static_cast<std::size_t>(i)];
            if (src >= 0 &&
                src < static_cast<int>(project.tracks.size()) &&
                project.tracks[static_cast<std::size_t>(src)].kind ==
                    kind) {
                candidates.push_back(src);
            }
        }
        if (clip->track >= 0 &&
            clip->track < static_cast<int>(project.tracks.size()) &&
            project.tracks[static_cast<std::size_t>(clip->track)]
                    .kind == kind) {
            candidates.push_back(clip->track);
        }
        for (int t = 0; t < static_cast<int>(project.tracks.size());
             ++t) {
            if (project.tracks[static_cast<std::size_t>(t)].kind ==
                kind) {
                candidates.push_back(t);
            }
        }
        // First candidate that is unused, unlocked and overlap-free
        // for every clip this inner track restores.
        int picked = -1;
        for (const int cand : candidates) {
            if (used[static_cast<std::size_t>(cand)]) {
                continue;
            }
            if (project.tracks[static_cast<std::size_t>(cand)].locked) {
                continue;
            }
            bool blocked = false;
            for (const Clip& inner : sequence->clips) {
                if (inner.track != i) {
                    continue;
                }
                const auto [s, e] = land_window(inner);
                if (overlaps_outer(cand, s, e)) {
                    blocked = true;
                    break;
                }
            }
            if (!blocked) {
                picked = cand;
                break;
            }
        }
        if (picked < 0) {
            // No existing lane fits — append one of the same kind
            // (name/gain follow the inner track).
            Track added = sequence->tracks[static_cast<std::size_t>(i)];
            added.locked = false;   // a locked source copy would trap
                                  // the restored clips
            project.tracks.push_back(added);
            used.push_back(false);
            picked = static_cast<int>(project.tracks.size()) - 1;
        }
        assign[static_cast<std::size_t>(i)] = picked;
        used[static_cast<std::size_t>(picked)] = true;
    }

    // ---- place the inner clips --------------------------------------
    // Inner clip ids may collide with project clip ids (e.g. clips
    // flattened, re-compounded, flattened again). Remint collisions and
    // translate inner transitions through the remap.
    std::map<std::string, std::string> id_map;
    std::set<std::string> taken_ids;
    for (const Clip& c : project.clips) {
        taken_ids.insert(c.id);
    }
    std::vector<Clip> restored;
    restored.reserve(sequence->clips.size());
    for (const Clip& inner : sequence->clips) {
        auto [start, end] = land_window(inner);
        Clip out = inner;
        out.track = assign[static_cast<std::size_t>(inner.track)];
        out.timeline_start = start;
        out.duration = (end - start);
        out.playback_rate = inner.playback_rate * rate;
        if (out.timeline_start < 0.0) {
            // Left of t=0 is unrepresentable — trim the head like a
            // TrimEdge::In clamp: shift in_point by the dropped
            // timeline seconds (scaled by the clip's own rate).
            const double dropped = -out.timeline_start;
            out.timeline_start = 0.0;
            out.duration -= dropped;
            out.in_point += dropped * out.playback_rate;
            if (out.duration <= 0.0) {
                continue;   // fully outside the pasteable range
            }
        }
        const bool remap = taken_ids.count(out.id) != 0;
        const std::string landed_id =
            remap ? NewClipId() : out.id;
        id_map[inner.id] = landed_id;
        taken_ids.insert(landed_id);
        out.id = landed_id;
        restored.push_back(std::move(out));
    }
    // Inner transitions between restored clips come along.
    std::vector<Transition> moved;
    for (const Transition& transition : sequence->transitions) {
        auto fi = id_map.find(transition.from_clip);
        auto ti = id_map.find(transition.to_clip);
        if (fi == id_map.end() || ti == id_map.end()) {
            continue;
        }
        Transition out = transition;
        out.from_clip = fi->second;
        out.to_clip = ti->second;
        moved.push_back(std::move(out));
    }

    // Commit: swap the compound for its restored content.
    std::erase_if(project.clips,
                  [&](const Clip& c) { return c.id == clip->id; });
    for (Clip& c : restored) {
        project.clips.push_back(std::move(c));
    }
    for (Transition& t : moved) {
        project.transitions.push_back(std::move(t));
    }
    // The sequence dies with its last reference; other compound clips
    // (top-level or nested) keep it alive.
    if (!SequenceInUse(project, sequence->id)) {
        std::erase_if(project.sequences, [&](const Sequence& s) {
            return s.id == sequence->id;
        });
    }
    DropInvalidTransitions(project);
    return true;
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

bool AddMarker(Project& project, const Marker& marker,
               std::string* error_out) {
    if (!std::isfinite(marker.time) || marker.time < 0.0) {
        return Fail(error_out, "marker time must be finite and >= 0");
    }
    for (auto& existing : project.markers) {
        if (std::fabs(existing.time - marker.time) <= kEpsilon) {
            existing = marker;
            return true;
        }
    }
    const auto pos = std::lower_bound(
        project.markers.begin(), project.markers.end(), marker.time,
        [](const Marker& m, double t) { return m.time < t; });
    project.markers.insert(pos, marker);
    return true;
}

bool RemoveMarker(Project& project, double time) {
    const auto it = std::find_if(
        project.markers.begin(), project.markers.end(),
        [&](const Marker& m) { return std::fabs(m.time - time) <= kEpsilon; });
    if (it == project.markers.end()) {
        return false;
    }
    project.markers.erase(it);
    return true;
}

bool SetWorkArea(Project& project, double in_seconds, double out_seconds,
                 std::string* error_out) {
    if (!std::isfinite(in_seconds) || !std::isfinite(out_seconds) ||
        in_seconds < 0.0 || out_seconds < 0.0) {
        return Fail(error_out, "work area must be finite and >= 0");
    }
    const double duration = project.duration();
    project.work_in = std::min(in_seconds, duration);
    project.work_out =
        out_seconds <= in_seconds ? 0.0 : std::min(out_seconds, duration);
    return true;
}

bool SetTrackLocked(Project& project, int track_index, bool locked,
                    std::string* error_out) {
    if (track_index < 0 ||
        track_index >= static_cast<int>(project.tracks.size())) {
        return Fail(error_out, "track index out of range");
    }
    project.tracks[static_cast<std::size_t>(track_index)].locked = locked;
    return true;
}

bool SetClipLinkGroup(Project& project, const std::string& clip_id,
                      const std::string& group) {
    Clip* clip = FindClip(project, clip_id);
    if (clip == nullptr) {
        return false;
    }
    clip->link_group = group;
    return true;
}

bool SetClipKeyframes(Project& project, const std::string& clip_id,
                      KeyProp prop, std::vector<Keyframe> keys,
                      std::string* error_out) {
    Clip* clip = FindClip(project, clip_id);
    if (clip == nullptr) {
        return Fail(error_out, "clip not found");
    }
    Clip updated = *clip;
    if (keys.empty()) {
        updated.keyframes.erase(prop);
    } else {
        std::sort(keys.begin(), keys.end(),
                  [](const Keyframe& a, const Keyframe& b) {
                      return a.time < b.time;
                  });
        updated.keyframes[prop] = std::move(keys);
    }
    if (!ValidateClipPlacement(project, updated, MediaBounds{}, clip_id,
                               error_out)) {
        return false;
    }
    *clip = std::move(updated);
    return true;
}

float EvaluateClipProperty(const Clip& clip, KeyProp prop, double u,
                           float fallback) {
    const auto it = clip.keyframes.find(prop);
    if (it == clip.keyframes.end() || it->second.empty()) {
        return fallback;
    }
    const std::vector<Keyframe>& keys = it->second;
    if (u <= keys.front().time) {
        return keys.front().value;
    }
    if (u >= keys.back().time) {
        return keys.back().value;
    }
    const auto next = std::upper_bound(
        keys.begin(), keys.end(), u,
        [](double t, const Keyframe& k) { return t < k.time; });
    const Keyframe& b = *next;
    const Keyframe& a = *(next - 1);
    if (a.easing == KeyEase::Hold) {
        return a.value;
    }
    const double span = b.time - a.time;
    double w = span > kEpsilon ? (u - a.time) / span : 1.0;
    w = std::clamp(w, 0.0, 1.0);
    switch (a.easing) {
        case KeyEase::EaseIn:
            w *= w;
            break;
        case KeyEase::EaseOut:
            w = 1.0 - (1.0 - w) * (1.0 - w);
            break;
        case KeyEase::EaseInOut:
            w = w * w * (3.0 - 2.0 * w);
            break;
        case KeyEase::EaseInCubic:
            w = w * w * w;
            break;
        case KeyEase::EaseOutCubic:
            w = 1.0 - (1.0 - w) * (1.0 - w) * (1.0 - w);
            break;
        case KeyEase::ExpoOut:
            w = w >= 1.0 ? 1.0 : 1.0 - std::pow(2.0, -10.0 * w);
            break;
        case KeyEase::Back: {
            // easeOutBack c1=1.70158 — passes through the target and
            // settles back (the overshoot look for motion/scale keys).
            constexpr double c = 1.70158;
            const double t = w - 1.0;
            w = 1.0 + (c + 1.0) * t * t * t + c * t * t;
            break;
        }
        case KeyEase::Elastic: {
            // easeOutElastic — rings around the target with decaying
            // amplitude; endpoints stay exact.
            if (w <= 0.0 || w >= 1.0) break;
            w = std::pow(2.0, -10.0 * w) *
                    std::sin((w * 10.0 - 0.75) *
                             (2.0 * 3.14159265358979323846) / 3.0) +
                1.0;
            break;
        }
        case KeyEase::Bounce: {
            // easeOutBounce — the standard four-hop landing curve.
            constexpr double n1 = 7.5625, d1 = 2.75;
            double p = w;
            if (p < 1.0 / d1) {
                w = n1 * p * p;
            } else if (p < 2.0 / d1) {
                p -= 1.5 / d1;
                w = n1 * p * p + 0.75;
            } else if (p < 2.5 / d1) {
                p -= 2.25 / d1;
                w = n1 * p * p + 0.9375;
            } else {
                p -= 2.625 / d1;
                w = n1 * p * p + 0.984375;
            }
            break;
        }
        default:
            break;  // Linear / Hold handled above
    }
    return a.value + static_cast<float>(w) * (b.value - a.value);
}

Transform EffectiveTransformAt(const Clip& clip, double u) {
    Transform t = clip.transform;
    t.offset_x =
        EvaluateClipProperty(clip, KeyProp::PosX, u, t.offset_x);
    t.offset_y =
        EvaluateClipProperty(clip, KeyProp::PosY, u, t.offset_y);
    t.scale = EvaluateClipProperty(clip, KeyProp::Scale, u, t.scale);
    t.rotation_deg =
        EvaluateClipProperty(clip, KeyProp::Rotation, u, t.rotation_deg);
    t.opacity =
        EvaluateClipProperty(clip, KeyProp::Opacity, u, t.opacity);
    return t;
}

float EffectiveGainDbAt(const Clip& clip, double u) {
    return EvaluateClipProperty(clip, KeyProp::GainDb, u, clip.gain_db);
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
