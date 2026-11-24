// Implementation of TimelineHistory — see timeline_history.h for the
// contract. Structure mirrors src/node_graph/node_graph_history.cc;
// the difference is the slice-restore model: clip commands snapshot
// the touched clips (vector index + full Clip) and the ENTIRE
// transitions vector before first Apply, then RestoreSlice puts both
// back verbatim on Revert.

#include "timeline_history.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace XER::VideoEditor {
namespace {

// What a clip-affecting command must restore on Revert. before_clips
// holds (vector_index, Clip) for exactly the ids the command touches;
// before_transitions is a FULL copy of project.transitions (small —
// transitions are few; full copy makes revert trivially correct and
// future-proof — today only transitions referencing the touched ids
// can break, but the full restore costs nothing). after_clip_ids
// lists clips to erase before reinserting before_clips (split's
// second half, added clips).
struct TimelineSlice {
    std::optional<std::vector<Track>> before_tracks;  // track commands only
    std::vector<std::pair<std::size_t, Clip>> before_clips;
    std::vector<Transition> before_transitions;
    std::vector<std::string> after_clip_ids;
};

TimelineSlice CaptureSlice(const Project& p,
                           const std::vector<std::string>& clip_ids,
                           bool include_tracks) {
    TimelineSlice s;
    if (include_tracks) {
        s.before_tracks = p.tracks;
    }
    s.before_transitions = p.transitions;
    for (const auto& id : clip_ids) {
        for (std::size_t i = 0; i < p.clips.size(); ++i) {
            if (p.clips[i].id == id) {
                s.before_clips.emplace_back(i, p.clips[i]);
                break;
            }
        }
    }
    return s;
}

void RestoreSlice(Project& p, const TimelineSlice& s) {
    if (s.before_tracks) {
        p.tracks = *s.before_tracks;
    }
    // Erase after-state clips, then restore before-state clips at their
    // recorded indices (clamped — later erases may shift positions).
    if (!s.after_clip_ids.empty()) {
        p.clips.erase(std::remove_if(p.clips.begin(), p.clips.end(),
            [&](const Clip& c) {
                return std::find(s.after_clip_ids.begin(),
                                 s.after_clip_ids.end(), c.id) !=
                       s.after_clip_ids.end();
            }), p.clips.end());
    }
    p.transitions = s.before_transitions;   // full restore
    auto restored = s.before_clips;         // sort ascending by index
    std::sort(restored.begin(), restored.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    for (const auto& [index, clip] : restored) {
        const std::size_t at = std::min(index, p.clips.size());
        p.clips.insert(p.clips.begin() + static_cast<std::ptrdiff_t>(at),
                       clip);
    }
}

// Exact transition-vector equality (fields compared with == — transition
// doubles are only ever copied or erased, never recomputed, so identical
// content is bit-identical). Used by MergeWith to detect whether a
// command's Apply changed the transition set.
bool TransitionsEqual(const std::vector<Transition>& a,
                      const std::vector<Transition>& b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        const Transition& x = a[i];
        const Transition& y = b[i];
        if (x.from_clip != y.from_clip || x.to_clip != y.to_clip ||
            x.type != y.type || x.duration != y.duration ||
            x.script != y.script) {
            return false;
        }
    }
    return true;
}

// Base for every clip/track mutation: captures the TimelineSlice once
// (first Apply — redo re-applies ApplyOp against the reverted state,
// which is exactly the before-state the slice describes), delegates
// the mutation to ApplyOp, and Revert restores the slice verbatim.
// Model ops are validate-then-mutate, so a failed ApplyOp leaves the
// project untouched and Execute drops the command.
class ClipEditCommand : public Command {
public:
    ClipEditCommand(std::vector<std::string> affected_ids,
                    bool include_tracks)
        : affected_ids_(std::move(affected_ids)),
          include_tracks_(include_tracks) {}

    bool Apply(Project& project) override {
        if (!captured_) {
            slice_ = CaptureSlice(project, affected_ids_, include_tracks_);
            captured_ = true;
        }
        std::string error;
        if (!ApplyOp(project, &error)) {
            last_error_ = std::move(error);
            return false;
        }
        last_error_.clear();
        // Post-apply transition snapshot — MergeWith consults it to
        // decide whether coalescing is replay-safe.
        after_transitions_ = project.transitions;
        return true;
    }

    bool Revert(Project& project) override {
        RestoreSlice(project, slice_);
        return true;
    }

    const std::string& last_error() const { return last_error_; }

protected:
    virtual bool ApplyOp(Project& project, std::string* error) = 0;
    // Called by ApplyOp on success to declare which clips exist only
    // in the after-state (added/split-off clips) so Revert can erase
    // them before restoring before_clips.
    virtual void NoteAfter(std::vector<std::string> ids) {
        slice_.after_clip_ids = std::move(ids);
    }

    const std::vector<std::string>& affected_ids() const {
        return affected_ids_;
    }

    // True when this command's Apply changed project.transitions
    // (DropInvalidTransitions side-effects). A merged command replays
    // only ONE ApplyOp on redo — it cannot reproduce intermediate
    // transition drops, so MergeWith must decline in that case.
    bool TransitionsChanged() const {
        return !TransitionsEqual(slice_.before_transitions,
                                 after_transitions_);
    }

    TimelineSlice slice_;
    std::vector<Transition> after_transitions_;
    bool captured_ = false;
    std::string last_error_;

private:
    std::vector<std::string> affected_ids_;
    bool include_tracks_ = false;
};

class AddClipCommand : public ClipEditCommand {
public:
    AddClipCommand(Clip clip, MediaBounds bounds)
        : ClipEditCommand({clip.id}, false),
          clip_(std::move(clip)),
          bounds_(bounds) {}
    bool ApplyOp(Project& project, std::string* error) override {
        if (!AddClip(project, clip_, bounds_, error)) {
            return false;
        }
        NoteAfter({clip_.id});
        return true;
    }
    std::string Label() const override { return "Add clip"; }

private:
    Clip clip_;
    MediaBounds bounds_;
};

class RemoveClipCommand : public ClipEditCommand {
public:
    explicit RemoveClipCommand(std::string id)
        : ClipEditCommand({id}, false), id_(std::move(id)) {}
    bool ApplyOp(Project& project, std::string*) override {
        // Model RemoveClip has no error_out — false means "not found".
        if (!RemoveClip(project, id_)) {
            return false;
        }
        NoteAfter({});   // nothing persists — the clip is gone
        return true;
    }
    std::string Label() const override { return "Remove clip"; }

private:
    std::string id_;
};

class MoveClipCommand : public ClipEditCommand {
public:
    MoveClipCommand(std::string id, double new_start, MediaBounds bounds)
        : ClipEditCommand({id}, false),
          id_(std::move(id)),
          new_start_(new_start),
          bounds_(bounds) {}
    bool ApplyOp(Project& project, std::string* error) override {
        if (!MoveClip(project, id_, new_start_, bounds_, error)) {
            return false;
        }
        NoteAfter({id_});
        return true;
    }
    std::string Label() const override { return "Move clip"; }
    // Consecutive moves of the same clip are one gesture (drag) — the
    // merged command keeps the FIRST captured slice (original state)
    // and adopts the LATEST target. Decline when either side changed
    // the transition set: merged redo replays a single ApplyOp and
    // cannot reproduce intermediate DropInvalidTransitions drops.
    bool MergeWith(const Command& other) override {
        const auto* m = dynamic_cast<const MoveClipCommand*>(&other);
        if (m == nullptr || m->id_ != id_) {
            return false;
        }
        if (TransitionsChanged() || m->TransitionsChanged()) {
            return false;
        }
        new_start_ = m->new_start_;
        return true;
    }

private:
    std::string id_;
    double new_start_;
    MediaBounds bounds_;
};

class TrimClipCommand : public ClipEditCommand {
public:
    TrimClipCommand(std::string id, TrimEdge edge, double new_point,
                    MediaBounds bounds)
        : ClipEditCommand({id}, false),
          id_(std::move(id)),
          edge_(edge),
          new_point_(new_point),
          bounds_(bounds) {}
    bool ApplyOp(Project& project, std::string* error) override {
        if (!TrimClip(project, id_, edge_, new_point_, bounds_, error)) {
            return false;
        }
        NoteAfter({id_});
        return true;
    }
    std::string Label() const override { return "Trim clip"; }
    // Coalesce only same-edge trims of the same clip — an in-trim
    // followed by an out-trim is two gestures. Decline when either
    // side changed the transition set (see MoveClipCommand::MergeWith).
    bool MergeWith(const Command& other) override {
        const auto* t = dynamic_cast<const TrimClipCommand*>(&other);
        if (t == nullptr || t->id_ != id_ || t->edge_ != edge_) {
            return false;
        }
        if (TransitionsChanged() || t->TransitionsChanged()) {
            return false;
        }
        new_point_ = t->new_point_;
        return true;
    }

private:
    std::string id_;
    TrimEdge edge_;
    double new_point_;
    MediaBounds bounds_;
};

class SplitClipCommand : public ClipEditCommand {
public:
    // new_id is generated ONCE by the wrapper and stored here, so redo
    // re-splits with the identical id — the resulting project is
    // byte-identical across undo→redo.
    SplitClipCommand(std::string id, double split_t, std::string new_id)
        : ClipEditCommand({id}, false),
          id_(std::move(id)),
          split_t_(split_t),
          new_id_(std::move(new_id)) {}
    bool ApplyOp(Project& project, std::string* error) override {
        if (!SplitClip(project, id_, split_t_, new_id_, error)) {
            return false;
        }
        // Both halves exist after-state: the (modified) original and
        // the split-off clip — Revert erases both, then reinserts the
        // captured pre-split clip.
        NoteAfter({id_, new_id_});
        return true;
    }
    std::string Label() const override { return "Split clip"; }

private:
    std::string id_;
    double split_t_;
    std::string new_id_;
};

class ReplaceClipCommand : public ClipEditCommand {
public:
    ReplaceClipCommand(std::string id, Clip replacement,
                       MediaBounds bounds)
        : ClipEditCommand({id}, false),
          id_(std::move(id)),
          replacement_(std::move(replacement)),
          bounds_(bounds) {}
    bool ApplyOp(Project& project, std::string* error) override {
        if (!ReplaceClip(project, id_, replacement_, bounds_, error)) {
            return false;
        }
        NoteAfter({id_});
        return true;
    }
    std::string Label() const override { return "Replace clip"; }
    // Inspector edits coalesce: the last replacement wins, the first
    // captured slice (pre-edit state) is kept for Revert. Decline when
    // either side changed the transition set (see MoveClipCommand).
    bool MergeWith(const Command& other) override {
        const auto* r = dynamic_cast<const ReplaceClipCommand*>(&other);
        if (r == nullptr || r->id_ != id_) {
            return false;
        }
        if (TransitionsChanged() || r->TransitionsChanged()) {
            return false;
        }
        replacement_ = r->replacement_;
        return true;
    }

private:
    std::string id_;
    Clip replacement_;
    MediaBounds bounds_;
};

class AddTrackCommand : public ClipEditCommand {
public:
    AddTrackCommand(TrackKind kind, std::string name)
        : ClipEditCommand({}, true), kind_(kind), name_(std::move(name)) {}
    bool ApplyOp(Project& project, std::string*) override {
        index_ = AddTrack(project, kind_, name_);   // always succeeds
        NoteAfter({});
        return true;
    }
    std::string Label() const override { return "Add track"; }
    int index() const { return index_; }

private:
    TrackKind kind_;
    std::string name_;
    int index_ = -1;
};

class RemoveTrackCommand : public ClipEditCommand {
public:
    // affected_ids = clips with track > index (their .track is
    // reindexed by the model op); include_tracks snapshots the track
    // table itself.
    RemoveTrackCommand(int track_index, std::vector<std::string> affected_ids)
        : ClipEditCommand(std::move(affected_ids), true),
          track_index_(track_index) {}
    bool ApplyOp(Project& project, std::string* error) override {
        if (!RemoveTrack(project, track_index_, error)) {
            return false;
        }
        // The affected clips persist (reindexed) — Revert erases them
        // and reinserts the captured pre-remove versions.
        NoteAfter(affected_ids());
        return true;
    }
    std::string Label() const override { return "Remove track"; }

private:
    int track_index_;
};

class RippleDeleteCommand : public ClipEditCommand {
public:
    // affected = the deleted clip + every clip on its track (the
    // followers shift left to close the gap).
    RippleDeleteCommand(std::string id,
                        std::vector<std::string> affected_ids)
        : ClipEditCommand(std::move(affected_ids), false),
          id_(std::move(id)) {}
    bool ApplyOp(Project& project, std::string* error) override {
        if (!RippleDeleteClip(project, id_, error)) {
            return false;
        }
        // The deleted clip is gone; every other affected clip persists
        // (shifted) — Revert erases them, then reinserts the captured
        // pre-delete versions (deleted clip included) at old indices.
        std::vector<std::string> after = affected_ids();
        after.erase(std::remove(after.begin(), after.end(), id_),
                    after.end());
        NoteAfter(std::move(after));
        return true;
    }
    std::string Label() const override { return "Ripple delete"; }

private:
    std::string id_;
};

// Base for ops that mint fresh clip ids INSIDE the model call
// (LiftRange boundary splits, OverwriteClipAt range clearing):
// re-running the op on redo would produce different generated ids, so
// the command snapshots the full clips+transitions vectors before AND
// after the first Apply and replays the after-state verbatim on redo
// (undo→redo is byte-identical — the SplitClipCommand determinism
// contract). The affected state is exactly clips+transitions: these
// ops touch nothing else.
class TimelineSnapshotCommand : public Command {
public:
    bool Apply(Project& project) override {
        if (captured_) {
            project.clips = after_clips_;
            project.transitions = after_transitions_;
            return true;
        }
        before_clips_ = project.clips;
        before_transitions_ = project.transitions;
        last_error_.clear();
        if (!ApplyOp(project, &last_error_)) {
            return false;
        }
        after_clips_ = project.clips;
        after_transitions_ = project.transitions;
        captured_ = true;
        return true;
    }
    bool Revert(Project& project) override {
        project.clips = before_clips_;
        project.transitions = before_transitions_;
        return true;
    }
    const std::string& last_error() const { return last_error_; }

protected:
    virtual bool ApplyOp(Project& project, std::string* error) = 0;

private:
    std::vector<Clip> before_clips_, after_clips_;
    std::vector<Transition> before_transitions_, after_transitions_;
    bool captured_ = false;
    std::string last_error_;
};

class LiftRangeCommand : public TimelineSnapshotCommand {
public:
    LiftRangeCommand(double t0, double t1, bool close_gap)
        : t0_(t0), t1_(t1), close_gap_(close_gap) {}
    bool ApplyOp(Project& project, std::string* error) override {
        return LiftRange(project, t0_, t1_, close_gap_, error);
    }
    std::string Label() const override { return "Lift/extract"; }

private:
    double t0_, t1_;
    bool close_gap_;
};

// Same snapshot/replay shape as TimelineSnapshotCommand but covering
// the FULL timeline state — tracks, clips, transitions AND the stored
// sequences. Compound ops touch all of them at once: MakeCompound
// moves clips into a new sequence and FlattenCompound can append
// tracks. First Apply runs the op live; undo/redo replay the exact
// captured vectors so freshly minted ids stay deterministic.
class ProjectSnapshotCommand : public Command {
public:
    bool Apply(Project& project) override {
        if (captured_) {
            project.tracks = after_tracks_;
            project.clips = after_clips_;
            project.transitions = after_transitions_;
            project.sequences = after_sequences_;
            return true;
        }
        before_tracks_ = project.tracks;
        before_clips_ = project.clips;
        before_transitions_ = project.transitions;
        before_sequences_ = project.sequences;
        last_error_.clear();
        if (!ApplyOp(project, &last_error_)) {
            return false;
        }
        after_tracks_ = project.tracks;
        after_clips_ = project.clips;
        after_transitions_ = project.transitions;
        after_sequences_ = project.sequences;
        captured_ = true;
        return true;
    }
    bool Revert(Project& project) override {
        project.tracks = before_tracks_;
        project.clips = before_clips_;
        project.transitions = before_transitions_;
        project.sequences = before_sequences_;
        return true;
    }
    const std::string& last_error() const { return last_error_; }

protected:
    virtual bool ApplyOp(Project& project, std::string* error) = 0;

private:
    std::vector<Track> before_tracks_, after_tracks_;
    std::vector<Clip> before_clips_, after_clips_;
    std::vector<Transition> before_transitions_, after_transitions_;
    std::vector<Sequence> before_sequences_, after_sequences_;
    bool captured_ = false;
    std::string last_error_;
};

class MakeCompoundCommand : public ProjectSnapshotCommand {
public:
    MakeCompoundCommand(std::vector<std::string> clip_ids,
                        std::string name, std::string clip_id,
                        std::string sequence_id)
        : clip_ids_(std::move(clip_ids)), name_(std::move(name)),
          clip_id_(std::move(clip_id)),
          sequence_id_(std::move(sequence_id)) {}
    bool ApplyOp(Project& project, std::string* error) override {
        return XER::VideoEditor::MakeCompound(
            project, clip_ids_, name_, clip_id_, sequence_id_, error);
    }
    std::string Label() const override { return "Compound clips"; }
    const std::string& clip_id() const { return clip_id_; }
    const std::string& sequence_id() const { return sequence_id_; }

private:
    std::vector<std::string> clip_ids_;
    std::string name_, clip_id_, sequence_id_;
};

class FlattenCompoundCommand : public ProjectSnapshotCommand {
public:
    explicit FlattenCompoundCommand(std::string clip_id)
        : clip_id_(std::move(clip_id)) {}
    bool ApplyOp(Project& project, std::string* error) override {
        return XER::VideoEditor::FlattenCompound(project, clip_id_,
                                                 error);
    }
    std::string Label() const override { return "Decompose compound"; }

private:
    std::string clip_id_;
};

class RollingEditCommand : public ClipEditCommand {
public:
    RollingEditCommand(std::string from, std::string to, double delta,
                       MediaBounds from_bounds, MediaBounds to_bounds)
        : ClipEditCommand({from, to}, false),
          from_(std::move(from)),
          to_(std::move(to)),
          delta_(delta),
          from_bounds_(from_bounds),
          to_bounds_(to_bounds) {}
    bool ApplyOp(Project& project, std::string* error) override {
        if (!RollingEdit(project, from_, to_, delta_, from_bounds_,
                         to_bounds_, error)) {
            return false;
        }
        NoteAfter({from_, to_});   // both persist (re-timed)
        return true;
    }
    std::string Label() const override { return "Rolling edit"; }
    // A rolling-edit drag is one gesture — consecutive rolls on the
    // same cut coalesce by summing deltas (rolling is linear in
    // delta). Same transition-change caveat as MoveClipCommand.
    bool MergeWith(const Command& other) override {
        const auto* r = dynamic_cast<const RollingEditCommand*>(&other);
        if (r == nullptr || r->from_ != from_ || r->to_ != to_) {
            return false;
        }
        if (TransitionsChanged() || r->TransitionsChanged()) {
            return false;
        }
        delta_ += r->delta_;
        return true;
    }

private:
    std::string from_;
    std::string to_;
    double delta_;
    MediaBounds from_bounds_;
    MediaBounds to_bounds_;
};

class SlipClipCommand : public ClipEditCommand {
public:
    SlipClipCommand(std::string id, double delta, MediaBounds bounds)
        : ClipEditCommand({id}, false),
          id_(std::move(id)),
          delta_(delta),
          bounds_(bounds) {}
    bool ApplyOp(Project& project, std::string* error) override {
        if (!SlipClip(project, id_, delta_, bounds_, error)) {
            return false;
        }
        NoteAfter({id_});
        return true;
    }
    std::string Label() const override { return "Slip clip"; }
    // Slips are linear in delta — a drag coalesces by summing.
    bool MergeWith(const Command& other) override {
        const auto* s = dynamic_cast<const SlipClipCommand*>(&other);
        if (s == nullptr || s->id_ != id_) {
            return false;
        }
        if (TransitionsChanged() || s->TransitionsChanged()) {
            return false;
        }
        delta_ += s->delta_;
        return true;
    }

private:
    std::string id_;
    double delta_;
    MediaBounds bounds_;
};

class SlideClipCommand : public ClipEditCommand {
public:
    // affected = the slid clip + every clip on its track (only the
    // immediate neighbours change, but capturing the whole track is
    // the established slice pattern and makes Revert trivially exact).
    SlideClipCommand(std::string id, double delta, MediaBounds bounds,
                     std::vector<std::string> affected_ids)
        : ClipEditCommand(std::move(affected_ids), false),
          id_(std::move(id)),
          delta_(delta),
          bounds_(bounds) {}
    bool ApplyOp(Project& project, std::string* error) override {
        if (!SlideClip(project, id_, delta_, bounds_, error)) {
            return false;
        }
        NoteAfter(affected_ids());   // nothing is added or removed
        return true;
    }
    std::string Label() const override { return "Slide clip"; }
    // Slides are linear in delta — a drag coalesces by summing.
    bool MergeWith(const Command& other) override {
        const auto* s = dynamic_cast<const SlideClipCommand*>(&other);
        if (s == nullptr || s->id_ != id_) {
            return false;
        }
        if (TransitionsChanged() || s->TransitionsChanged()) {
            return false;
        }
        delta_ += s->delta_;
        return true;
    }

private:
    std::string id_;
    double delta_;
    MediaBounds bounds_;
};

class SetClipRateCommand : public ClipEditCommand {
public:
    SetClipRateCommand(std::string id, double rate, MediaBounds bounds)
        : ClipEditCommand({id}, false),
          id_(std::move(id)),
          rate_(rate),
          bounds_(bounds) {}
    bool ApplyOp(Project& project, std::string* error) override {
        if (!SetClipRate(project, id_, rate_, bounds_, error)) {
            return false;
        }
        NoteAfter({id_});
        return true;
    }
    std::string Label() const override { return "Set speed"; }

private:
    std::string id_;
    double rate_;
    MediaBounds bounds_;
};

class InsertClipAtCommand : public ClipEditCommand {
public:
    // affected = the new clip + every clip on its track (followers
    // shift right to open the insert gap).
    InsertClipAtCommand(Clip clip, MediaBounds bounds,
                        std::vector<std::string> affected_ids)
        : ClipEditCommand(std::move(affected_ids), false),
          clip_(std::move(clip)),
          bounds_(bounds) {}
    bool ApplyOp(Project& project, std::string* error) override {
        if (!InsertClipAt(project, clip_, bounds_, error)) {
            return false;
        }
        NoteAfter(affected_ids());   // shifted clips + the new clip
        return true;
    }
    std::string Label() const override { return "Insert clip"; }

private:
    Clip clip_;
    MediaBounds bounds_;
};

class OverwriteClipAtCommand : public TimelineSnapshotCommand {
public:
    OverwriteClipAtCommand(Clip clip, MediaBounds bounds)
        : clip_(std::move(clip)), bounds_(bounds) {}
    bool ApplyOp(Project& project, std::string* error) override {
        return OverwriteClipAt(project, clip_, bounds_, error);
    }
    std::string Label() const override { return "Overwrite clip"; }

private:
    Clip clip_;
    MediaBounds bounds_;
};

class SetClipLinkGroupCommand : public ClipEditCommand {
public:
    SetClipLinkGroupCommand(std::string id, std::string group)
        : ClipEditCommand({id}, false),
          id_(std::move(id)),
          group_(std::move(group)) {}
    bool ApplyOp(Project& project, std::string*) override {
        if (!SetClipLinkGroup(project, id_, group_)) {
            return false;   // clip not found — model op has no error_out
        }
        NoteAfter({id_});
        return true;
    }
    std::string Label() const override { return "Link clips"; }

private:
    std::string id_;
    std::string group_;
};

class SetClipKeyframesCommand : public ClipEditCommand {
public:
    SetClipKeyframesCommand(std::string id, KeyProp prop,
                            std::vector<Keyframe> keys)
        : ClipEditCommand({id}, false),
          id_(std::move(id)),
          prop_(prop),
          keys_(std::move(keys)) {}
    bool ApplyOp(Project& project, std::string* error) override {
        if (!SetClipKeyframes(project, id_, prop_, keys_, error)) {
            return false;
        }
        NoteAfter({id_});
        return true;
    }
    std::string Label() const override { return "Set keyframes"; }

private:
    std::string id_;
    KeyProp prop_;
    std::vector<Keyframe> keys_;
};

// Track-lock toggles are dedicated (like the transition commands):
// they mutate a single Track field — outside the clip slice — so the
// command stores the prior flag and restores it on Revert.
class SetTrackLockedCommand : public Command {
public:
    SetTrackLockedCommand(int track_index, bool locked)
        : track_index_(track_index), locked_(locked) {}
    bool Apply(Project& project) override {
        last_error_.clear();
        if (track_index_ < 0 ||
            track_index_ >= static_cast<int>(project.tracks.size())) {
            last_error_ = "track index out of range";
            return false;
        }
        before_ = project.tracks[static_cast<std::size_t>(track_index_)]
                      .locked;
        return SetTrackLocked(project, track_index_, locked_,
                              &last_error_);
    }
    bool Revert(Project& project) override {
        if (track_index_ >= 0 &&
            track_index_ < static_cast<int>(project.tracks.size())) {
            project.tracks[static_cast<std::size_t>(track_index_)]
                .locked = before_;
        }
        return true;
    }
    std::string Label() const override { return "Toggle track lock"; }
    const std::string& last_error() const { return last_error_; }

private:
    int track_index_;
    bool locked_;
    bool before_ = false;
    std::string last_error_;
};

// Track mute/hide — bool flag ops mirroring SetTrackLockedCommand but
// touching fields that don't gate other ops, so no model helper is
// needed. `which`: 'm' = muted, 'h' = hidden.
class SetTrackFlagCommand : public Command {
public:
    SetTrackFlagCommand(int track_index, char which, bool on)
        : track_index_(track_index), which_(which), on_(on) {}
    bool Apply(Project& project) override {
        last_error_.clear();
        if (track_index_ < 0 ||
            track_index_ >= static_cast<int>(project.tracks.size())) {
            last_error_ = "track index out of range";
            return false;
        }
        Track& track = project.tracks[
            static_cast<std::size_t>(track_index_)];
        before_ = which_ == 'm' ? track.muted : track.hidden;
        (which_ == 'm' ? track.muted : track.hidden) = on_;
        return true;
    }
    bool Revert(Project& project) override {
        if (track_index_ >= 0 &&
            track_index_ < static_cast<int>(project.tracks.size())) {
            Track& track = project.tracks[
                static_cast<std::size_t>(track_index_)];
            (which_ == 'm' ? track.muted : track.hidden) = before_;
        }
        return true;
    }
    std::string Label() const override {
        return which_ == 'm' ? "Toggle track mute"
                             : "Toggle track visibility";
    }
    const std::string& last_error() const { return last_error_; }

private:
    int track_index_;
    char which_;
    bool on_;
    bool before_ = false;
    std::string last_error_;
};

// Track fader/pan and the project master fader — continuous-write ops
// that coalesce via MergeWith so a knob drag folds into one undo
// step. `which`: 'g' = Track::gain_db, 'p' = Track::pan,
// 'M' = Project::master_gain_db (track_index_ ignored).
class SetGainLikeCommand : public Command {
public:
    SetGainLikeCommand(char which, int track_index, float value)
        : which_(which), track_index_(track_index), value_(value) {}
    bool Apply(Project& project) override {
        last_error_.clear();
        if (!std::isfinite(value_)) {
            last_error_ = "value must be finite";
            return false;
        }
        if (which_ == 'M') {
            if (value_ < -96.0f || value_ > 24.0f) {
                last_error_ = "master gain out of range [-96, 24]";
                return false;
            }
            before_ = project.master_gain_db;
            project.master_gain_db = value_;
            return true;
        }
        if (track_index_ < 0 ||
            track_index_ >= static_cast<int>(project.tracks.size())) {
            last_error_ = "track index out of range";
            return false;
        }
        Track& track = project.tracks[
            static_cast<std::size_t>(track_index_)];
        if (which_ == 'p') {
            if (value_ < -1.0f || value_ > 1.0f) {
                last_error_ = "pan out of range [-1, 1]";
                return false;
            }
            before_ = track.pan;
            track.pan = value_;
        } else {
            before_ = track.gain_db;
            track.gain_db = value_;
        }
        return true;
    }
    bool Revert(Project& project) override {
        if (which_ == 'M') {
            project.master_gain_db = before_;
            return true;
        }
        if (track_index_ >= 0 &&
            track_index_ < static_cast<int>(project.tracks.size())) {
            Track& track = project.tracks[
                static_cast<std::size_t>(track_index_)];
            (which_ == 'p' ? track.pan : track.gain_db) = before_;
        }
        return true;
    }
    bool MergeWith(const Command& other) override {
        // PushBack asks the STACKED command (this) to absorb the new
        // one (other): keep our pre-drag `before_`, adopt the newest
        // target value — a fader drag replays as a single undo step.
        const auto* o = dynamic_cast<const SetGainLikeCommand*>(&other);
        if (o == nullptr || o->which_ != which_ ||
            o->track_index_ != track_index_) {
            return false;
        }
        value_ = o->value_;
        return true;
    }
    std::string Label() const override {
        switch (which_) {
            case 'M': return "Set master gain";
            case 'p': return "Set track pan";
            default:  return "Set track gain";
        }
    }
    const std::string& last_error() const { return last_error_; }

private:
    char which_;
    int track_index_;
    float value_;
    float before_ = 0.0f;
    std::string last_error_;
};

// Marker ops are dedicated: project.markers sits outside the clip
// slice, so the whole (small) vector is snapshotted before Apply and
// restored verbatim on Revert — exact for both add and remove.
class AddMarkerCommand : public Command {
public:
    explicit AddMarkerCommand(Marker marker)
        : marker_(std::move(marker)) {}
    bool Apply(Project& project) override {
        before_ = project.markers;
        last_error_.clear();
        return AddMarker(project, marker_, &last_error_);
    }
    bool Revert(Project& project) override {
        project.markers = before_;
        return true;
    }
    std::string Label() const override { return "Add marker"; }
    const std::string& last_error() const { return last_error_; }

private:
    Marker marker_;
    std::vector<Marker> before_;
    std::string last_error_;
};

class RemoveMarkerCommand : public Command {
public:
    explicit RemoveMarkerCommand(double time) : time_(time) {}
    bool Apply(Project& project) override {
        before_ = project.markers;
        return RemoveMarker(project, time_);
    }
    bool Revert(Project& project) override {
        project.markers = before_;
        return true;
    }
    std::string Label() const override { return "Remove marker"; }

private:
    double time_;
    std::vector<Marker> before_;
};

// Work-area changes are dedicated: two scalar fields snapshot-restored
// on Revert.
class SetWorkAreaCommand : public Command {
public:
    SetWorkAreaCommand(double in_seconds, double out_seconds)
        : in_(in_seconds), out_(out_seconds) {}
    bool Apply(Project& project) override {
        before_in_ = project.work_in;
        before_out_ = project.work_out;
        last_error_.clear();
        return SetWorkArea(project, in_, out_, &last_error_);
    }
    bool Revert(Project& project) override {
        project.work_in = before_in_;
        project.work_out = before_out_;
        return true;
    }
    std::string Label() const override { return "Set work area"; }
    const std::string& last_error() const { return last_error_; }

private:
    double in_, out_;
    double before_in_ = 0.0, before_out_ = 0.0;
    std::string last_error_;
};

// Transition commands are dedicated (not slice-based): they mutate
// project.transitions directly and restore the exact Transition.
class AddTransitionCommand : public Command {
public:
    AddTransitionCommand(std::string from, std::string to,
                         Transition::Type type, double duration,
                         std::string script, std::string xfade,
                         std::string xer_fx, std::string xer_params)
        : from_(std::move(from)),
          to_(std::move(to)),
          type_(type),
          duration_(duration),
          script_(std::move(script)),
          xfade_(std::move(xfade)),
          xer_fx_(std::move(xer_fx)),
          xer_params_(std::move(xer_params)) {}
    bool Apply(Project& project) override {
        last_error_.clear();
        return AddTransition(project, from_, to_, type_, duration_,
                             &last_error_, script_, xfade_, xer_fx_,
                             xer_params_);
    }
    bool Revert(Project& project) override {
        return RemoveTransition(project, from_, to_);
    }
    std::string Label() const override { return "Add transition"; }
    const std::string& last_error() const { return last_error_; }

private:
    std::string from_;
    std::string to_;
    Transition::Type type_;
    double duration_;
    std::string script_;
    std::string xfade_;
    std::string xer_fx_;
    std::string xer_params_;
    std::string last_error_;
};

// Duration retarget — same dedicated shape as the add/remove pair:
// the before-duration is captured on first Apply and restored by
// (from,to) lookup on Revert (LIFO guarantees the junction is still
// there). Consecutive sets on the same junction coalesce — a live
// commit stream folds into one undo step like a fader drag.
class SetTransitionDurationCommand : public Command {
public:
    SetTransitionDurationCommand(std::string from, std::string to,
                                 double duration)
        : from_(std::move(from)), to_(std::move(to)),
          duration_(duration) {}
    bool Apply(Project& project) override {
        last_error_.clear();
        for (const auto& t : project.transitions) {
            if (t.from_clip == from_ && t.to_clip == to_) {
                before_ = t.duration;
                found_ = true;
                break;
            }
        }
        if (!found_) {
            last_error_ = "transition not found";
            return false;
        }
        return SetTransitionDuration(project, from_, to_, duration_,
                                     &last_error_);
    }
    bool Revert(Project& project) override {
        for (auto& t : project.transitions) {
            if (t.from_clip == from_ && t.to_clip == to_) {
                t.duration = before_;
                return true;
            }
        }
        return false;
    }
    std::string Label() const override {
        return "Set transition duration";
    }
    bool MergeWith(const Command& other) override {
        const auto* o =
            dynamic_cast<const SetTransitionDurationCommand*>(&other);
        if (o == nullptr || o->from_ != from_ || o->to_ != to_) {
            return false;
        }
        duration_ = o->duration_;   // keep our before_, take the newest
        return true;
    }
    const std::string& last_error() const { return last_error_; }

private:
    std::string from_;
    std::string to_;
    double duration_;
    double before_ = 0.0;
    bool found_ = false;
    std::string last_error_;
};

class RemoveTransitionCommand : public Command {
public:
    RemoveTransitionCommand(std::string from, std::string to)
        : from_(std::move(from)), to_(std::move(to)) {}
    bool Apply(Project& project) override {
        found_ = false;
        for (std::size_t i = 0; i < project.transitions.size(); ++i) {
            const Transition& t = project.transitions[i];
            if (t.from_clip == from_ && t.to_clip == to_) {
                transition_ = t;
                index_ = i;
                found_ = true;
                break;
            }
        }
        if (!found_) {
            return false;
        }
        return RemoveTransition(project, from_, to_);
    }
    bool Revert(Project& project) override {
        // LIFO guarantees the prior state — the transition was valid
        // there, so reinsert it verbatim at its captured position
        // (no re-validation needed; order preserved for exact restore).
        const std::size_t at = std::min(index_, project.transitions.size());
        project.transitions.insert(
            project.transitions.begin() + static_cast<std::ptrdiff_t>(at),
            transition_);
        return true;
    }
    std::string Label() const override { return "Remove transition"; }

private:
    std::string from_;
    std::string to_;
    Transition transition_;
    std::size_t index_ = 0;
    bool found_ = false;
};

}  // namespace

class TimelineHistory::MacroCommand : public Command {
public:
    explicit MacroCommand(std::string label) : label_(std::move(label)) {}
    bool Apply(Project& project) override {
        // Transaction semantics: a macro applies all-or-nothing. If a
        // sub-op fails at index i, ops [0, i) are reverted in reverse
        // order — a half-applied macro with no undo entry would leave
        // unrecoverable project corruption on every redo (bug-audit
        // finding). Revert failures are ignored: each Revert restores
        // the state its own Apply captured, which is still what the
        // project holds.
        for (std::size_t i = 0; i < commands_.size(); ++i) {
            if (!commands_[i]->Apply(project)) {
                while (i-- > 0) {
                    commands_[i]->Revert(project);
                }
                return false;
            }
        }
        return true;
    }
    bool Revert(Project& project) override {
        for (auto it = commands_.rbegin(); it != commands_.rend(); ++it) {
            (*it)->Revert(project);
        }
        return true;
    }
    std::string Label() const override { return label_; }
    void Add(std::unique_ptr<Command> c) { commands_.push_back(std::move(c)); }
    bool empty() const { return commands_.empty(); }

private:
    std::string label_;
    std::vector<std::unique_ptr<Command>> commands_;
};

TimelineHistory::TimelineHistory(Project& project, std::size_t max_depth)
    : project_(project), max_depth_(std::max<std::size_t>(1, max_depth)) {}

TimelineHistory::~TimelineHistory() = default;

void TimelineHistory::PushBack(std::unique_ptr<Command> command) {
    redo_.clear();
    if (!macro_stack_.empty()) {
        macro_stack_.back()->Add(std::move(command));
        return;
    }
    if (!undo_.empty() && undo_.back()->MergeWith(*command)) {
        return;
    }
    undo_.push_back(std::move(command));
    while (undo_.size() > max_depth_) {
        undo_.pop_front();
    }
}

bool TimelineHistory::Execute(std::unique_ptr<Command> command) {
    if (command == nullptr || !command->Apply(project_)) {
        return false;
    }
    PushBack(std::move(command));
    return true;
}

bool TimelineHistory::Undo() {
    if (undo_.empty() || !macro_stack_.empty()) {
        return false;
    }
    auto cmd = std::move(undo_.back());
    undo_.pop_back();
    cmd->Revert(project_);
    redo_.push_back(std::move(cmd));
    return true;
}

bool TimelineHistory::Redo() {
    if (redo_.empty() || !macro_stack_.empty()) {
        return false;
    }
    auto cmd = std::move(redo_.back());
    redo_.pop_back();
    if (!cmd->Apply(project_)) {
        // A failed apply must not silently discard the command: a
        // MacroCommand that partially applied would mutate the project
        // with no undo entry (its Apply now rolls back, but a non-macro
        // command could still fail mid-mutation). Keep it on redo_ so
        // the entry is neither lost nor re-queued into undo_ — Redo
        // reports failure, the caller can Undo or fix the cause and
        // retry.
        redo_.push_back(std::move(cmd));
        return false;
    }
    undo_.push_back(std::move(cmd));
    return true;
}

std::string TimelineHistory::UndoLabel() const {
    return undo_.empty() ? "" : undo_.back()->Label();
}

std::string TimelineHistory::RedoLabel() const {
    return redo_.empty() ? "" : redo_.back()->Label();
}

std::vector<std::string> TimelineHistory::UndoLabels() const {
    std::vector<std::string> labels;
    labels.reserve(undo_.size());
    for (const auto& cmd : undo_) labels.push_back(cmd->Label());
    return labels;
}

std::vector<std::string> TimelineHistory::RedoLabels() const {
    std::vector<std::string> labels;
    labels.reserve(redo_.size());
    for (auto it = redo_.rbegin(); it != redo_.rend(); ++it) {
        labels.push_back((*it)->Label());
    }
    return labels;
}

void TimelineHistory::Clear() {
    undo_.clear();
    redo_.clear();
    macro_stack_.clear();
}

void TimelineHistory::BeginMacro(const std::string& label) {
    macro_stack_.push_back(std::make_unique<MacroCommand>(label));
}

void TimelineHistory::EndMacro() {
    if (macro_stack_.empty()) {
        return;
    }
    auto macro = std::move(macro_stack_.back());
    macro_stack_.pop_back();
    if (macro->empty()) {
        return;
    }
    if (!macro_stack_.empty()) {
        macro_stack_.back()->Add(std::move(macro));
        return;
    }
    undo_.push_back(std::move(macro));
    while (undo_.size() > max_depth_) {
        undo_.pop_front();
    }
}

bool TimelineHistory::AddClip(Clip clip, const MediaBounds& bounds,
                              std::string* clip_id_out,
                              std::string* error_out) {
    if (clip.id.empty()) {
        clip.id = NewClipId();   // once — redo reuses the stored clip
    }
    const std::string id = clip.id;
    auto cmd = std::make_unique<AddClipCommand>(std::move(clip), bounds);
    auto* raw = cmd.get();
    if (!cmd->Apply(project_)) {
        if (error_out) {
            *error_out = raw->last_error();
        }
        return false;
    }
    PushBack(std::move(cmd));
    if (clip_id_out) {
        *clip_id_out = id;
    }
    return true;
}

bool TimelineHistory::RemoveClip(const std::string& clip_id) {
    return Execute(std::make_unique<RemoveClipCommand>(clip_id));
}

bool TimelineHistory::MoveClip(const std::string& clip_id,
                               double new_timeline_start,
                               const MediaBounds& bounds,
                               std::string* error_out) {
    auto cmd = std::make_unique<MoveClipCommand>(clip_id, new_timeline_start,
                                               bounds);
    auto* raw = cmd.get();
    if (!cmd->Apply(project_)) {
        if (error_out) {
            *error_out = raw->last_error();
        }
        return false;
    }
    PushBack(std::move(cmd));
    return true;
}

bool TimelineHistory::TrimClip(const std::string& clip_id, TrimEdge edge,
                               double new_point, const MediaBounds& bounds,
                               std::string* error_out) {
    auto cmd = std::make_unique<TrimClipCommand>(clip_id, edge, new_point,
                                               bounds);
    auto* raw = cmd.get();
    if (!cmd->Apply(project_)) {
        if (error_out) {
            *error_out = raw->last_error();
        }
        return false;
    }
    PushBack(std::move(cmd));
    return true;
}

bool TimelineHistory::SplitClip(const std::string& clip_id,
                                double split_timeline_t,
                                std::string* new_clip_id_out,
                                std::string* error_out) {
    // Generated ONCE here — the command stores it, so undo→redo
    // re-splits with the identical id (deterministic project state).
    const std::string new_id = NewClipId();
    auto cmd = std::make_unique<SplitClipCommand>(clip_id, split_timeline_t,
                                                new_id);
    auto* raw = cmd.get();
    if (!cmd->Apply(project_)) {
        if (error_out) {
            *error_out = raw->last_error();
        }
        return false;
    }
    PushBack(std::move(cmd));
    if (new_clip_id_out) {
        *new_clip_id_out = new_id;
    }
    return true;
}

bool TimelineHistory::ReplaceClip(const std::string& clip_id,
                                  const Clip& replacement,
                                  const MediaBounds& bounds,
                                  std::string* error_out) {
    auto cmd = std::make_unique<ReplaceClipCommand>(clip_id, replacement,
                                                  bounds);
    auto* raw = cmd.get();
    if (!cmd->Apply(project_)) {
        if (error_out) {
            *error_out = raw->last_error();
        }
        return false;
    }
    PushBack(std::move(cmd));
    return true;
}

bool TimelineHistory::AddTransition(const std::string& from_clip,
                                    const std::string& to_clip,
                                    Transition::Type type, double duration,
                                    std::string* error_out,
                                    const std::string& script,
                                    const std::string& xfade,
                                    const std::string& xer_fx,
                                    const std::string& xer_params) {
    auto cmd = std::make_unique<AddTransitionCommand>(
        from_clip, to_clip, type, duration, script, xfade, xer_fx,
        xer_params);
    auto* raw = cmd.get();
    if (!cmd->Apply(project_)) {
        if (error_out) {
            *error_out = raw->last_error();
        }
        return false;
    }
    PushBack(std::move(cmd));
    return true;
}

bool TimelineHistory::RemoveTransition(const std::string& from_clip,
                                       const std::string& to_clip) {
    return Execute(
        std::make_unique<RemoveTransitionCommand>(from_clip, to_clip));
}

bool TimelineHistory::SetTransitionDuration(
    const std::string& from_clip, const std::string& to_clip,
    double duration, std::string* error_out) {
    auto cmd = std::make_unique<SetTransitionDurationCommand>(
        from_clip, to_clip, duration);
    auto* raw = cmd.get();
    if (!cmd->Apply(project_)) {
        if (error_out) {
            *error_out = raw->last_error();
        }
        return false;
    }
    PushBack(std::move(cmd));
    return true;
}

int TimelineHistory::AddTrack(TrackKind kind, const std::string& name) {
    auto cmd = std::make_unique<AddTrackCommand>(kind, name);
    auto* raw = cmd.get();
    if (!cmd->Apply(project_)) {
        return -1;
    }
    PushBack(std::move(cmd));
    return raw->index();   // pointee outlives the move into undo_/macro
}

bool TimelineHistory::RemoveTrack(int track_index, std::string* error_out) {
    // Clips with track > index get reindexed — they are the affected
    // set the slice must capture/restore.
    std::vector<std::string> affected;
    for (const auto& clip : project_.clips) {
        if (clip.track > track_index) {
            affected.push_back(clip.id);
        }
    }
    auto cmd = std::make_unique<RemoveTrackCommand>(track_index,
                                                  std::move(affected));
    auto* raw = cmd.get();
    if (!cmd->Apply(project_)) {
        if (error_out) {
            *error_out = raw->last_error();
        }
        return false;
    }
    PushBack(std::move(cmd));
    return true;
}

bool TimelineHistory::RippleDeleteClip(const std::string& clip_id,
                                       std::string* error_out) {
    // The deleted clip plus every clip on its track — followers shift
    // when the gap closes, so the slice must cover all of them.
    std::vector<std::string> affected{clip_id};
    if (const Clip* clip = FindClip(project_, clip_id)) {
        for (const auto& other : project_.clips) {
            if (other.track == clip->track && other.id != clip_id) {
                affected.push_back(other.id);
            }
        }
    }
    auto cmd = std::make_unique<RippleDeleteCommand>(clip_id,
                                                     std::move(affected));
    auto* raw = cmd.get();
    if (!cmd->Apply(project_)) {
        if (error_out) {
            *error_out = raw->last_error();
        }
        return false;
    }
    PushBack(std::move(cmd));
    return true;
}

bool TimelineHistory::LiftRange(double t0, double t1, bool close_gap,
                                std::string* error_out) {
    auto cmd = std::make_unique<LiftRangeCommand>(t0, t1, close_gap);
    auto* raw = cmd.get();
    if (!cmd->Apply(project_)) {
        if (error_out) {
            *error_out = raw->last_error();
        }
        return false;
    }
    PushBack(std::move(cmd));
    return true;
}

bool TimelineHistory::MakeCompound(std::vector<std::string> clip_ids,
                                   std::string name,
                                   std::string* clip_id_out,
                                   std::string* sequence_id_out,
                                   std::string* error_out) {
    // Caller-supplied ids keep the snapshot command's redo replay
    // deterministic — the model op stores exactly these.
    auto cmd = std::make_unique<MakeCompoundCommand>(
        std::move(clip_ids), std::move(name), NewClipId(), NewClipId());
    auto* raw = cmd.get();
    if (!cmd->Apply(project_)) {
        if (error_out) {
            *error_out = raw->last_error();
        }
        return false;
    }
    if (clip_id_out != nullptr) {
        *clip_id_out = raw->clip_id();
    }
    if (sequence_id_out != nullptr) {
        *sequence_id_out = raw->sequence_id();
    }
    PushBack(std::move(cmd));
    return true;
}

bool TimelineHistory::FlattenCompound(const std::string& clip_id,
                                      std::string* error_out) {
    auto cmd = std::make_unique<FlattenCompoundCommand>(clip_id);
    auto* raw = cmd.get();
    if (!cmd->Apply(project_)) {
        if (error_out) {
            *error_out = raw->last_error();
        }
        return false;
    }
    PushBack(std::move(cmd));
    return true;
}

bool TimelineHistory::RollingEdit(const std::string& from_clip,
                                  const std::string& to_clip,
                                  double delta_seconds,
                                  const MediaBounds& from_bounds,
                                  const MediaBounds& to_bounds,
                                  std::string* error_out) {
    auto cmd = std::make_unique<RollingEditCommand>(
        from_clip, to_clip, delta_seconds, from_bounds, to_bounds);
    auto* raw = cmd.get();
    if (!cmd->Apply(project_)) {
        if (error_out) {
            *error_out = raw->last_error();
        }
        return false;
    }
    PushBack(std::move(cmd));
    return true;
}

bool TimelineHistory::SlipClip(const std::string& clip_id,
                               double delta_seconds,
                               const MediaBounds& bounds,
                               std::string* error_out) {
    auto cmd = std::make_unique<SlipClipCommand>(clip_id, delta_seconds,
                                                 bounds);
    auto* raw = cmd.get();
    if (!cmd->Apply(project_)) {
        if (error_out) {
            *error_out = raw->last_error();
        }
        return false;
    }
    PushBack(std::move(cmd));
    return true;
}

bool TimelineHistory::SlideClip(const std::string& clip_id,
                                double delta_seconds,
                                const MediaBounds& bounds,
                                std::string* error_out) {
    // The slid clip plus every clip on its track — the neighbours that
    // absorb the slide live in that set.
    std::vector<std::string> affected{clip_id};
    if (const Clip* clip = FindClip(project_, clip_id)) {
        for (const auto& other : project_.clips) {
            if (other.track == clip->track && other.id != clip_id) {
                affected.push_back(other.id);
            }
        }
    }
    auto cmd = std::make_unique<SlideClipCommand>(
        clip_id, delta_seconds, bounds, std::move(affected));
    auto* raw = cmd.get();
    if (!cmd->Apply(project_)) {
        if (error_out) {
            *error_out = raw->last_error();
        }
        return false;
    }
    PushBack(std::move(cmd));
    return true;
}

bool TimelineHistory::SetClipRate(const std::string& clip_id,
                                  double rate, const MediaBounds& bounds,
                                  std::string* error_out) {
    auto cmd = std::make_unique<SetClipRateCommand>(clip_id, rate,
                                                    bounds);
    auto* raw = cmd.get();
    if (!cmd->Apply(project_)) {
        if (error_out) {
            *error_out = raw->last_error();
        }
        return false;
    }
    PushBack(std::move(cmd));
    return true;
}

bool TimelineHistory::InsertClipAt(Clip clip, const MediaBounds& bounds,
                                   std::string* clip_id_out,
                                   std::string* error_out) {
    if (clip.id.empty()) {
        clip.id = NewClipId();   // once — redo reuses the stored clip
    }
    const std::string id = clip.id;
    std::vector<std::string> affected{id};
    for (const auto& other : project_.clips) {
        if (other.track == clip.track) {
            affected.push_back(other.id);
        }
    }
    auto cmd = std::make_unique<InsertClipAtCommand>(
        std::move(clip), bounds, std::move(affected));
    auto* raw = cmd.get();
    if (!cmd->Apply(project_)) {
        if (error_out) {
            *error_out = raw->last_error();
        }
        return false;
    }
    PushBack(std::move(cmd));
    if (clip_id_out) {
        *clip_id_out = id;
    }
    return true;
}

bool TimelineHistory::OverwriteClipAt(Clip clip,
                                      const MediaBounds& bounds,
                                      std::string* clip_id_out,
                                      std::string* error_out) {
    if (clip.id.empty()) {
        clip.id = NewClipId();
    }
    const std::string id = clip.id;
    auto cmd = std::make_unique<OverwriteClipAtCommand>(std::move(clip),
                                                      bounds);
    auto* raw = cmd.get();
    if (!cmd->Apply(project_)) {
        if (error_out) {
            *error_out = raw->last_error();
        }
        return false;
    }
    PushBack(std::move(cmd));
    if (clip_id_out) {
        *clip_id_out = id;
    }
    return true;
}

bool TimelineHistory::AddMarker(const Marker& marker,
                                std::string* error_out) {
    auto cmd = std::make_unique<AddMarkerCommand>(marker);
    auto* raw = cmd.get();
    if (!cmd->Apply(project_)) {
        if (error_out) {
            *error_out = raw->last_error();
        }
        return false;
    }
    PushBack(std::move(cmd));
    return true;
}

bool TimelineHistory::RemoveMarker(double time) {
    return Execute(std::make_unique<RemoveMarkerCommand>(time));
}

bool TimelineHistory::SetWorkArea(double in_seconds, double out_seconds,
                                  std::string* error_out) {
    auto cmd = std::make_unique<SetWorkAreaCommand>(in_seconds,
                                                    out_seconds);
    auto* raw = cmd.get();
    if (!cmd->Apply(project_)) {
        if (error_out) {
            *error_out = raw->last_error();
        }
        return false;
    }
    PushBack(std::move(cmd));
    return true;
}

bool TimelineHistory::SetTrackLocked(int track_index, bool locked,
                                     std::string* error_out) {
    auto cmd = std::make_unique<SetTrackLockedCommand>(track_index,
                                                       locked);
    auto* raw = cmd.get();
    if (!cmd->Apply(project_)) {
        if (error_out) {
            *error_out = raw->last_error();
        }
        return false;
    }
    PushBack(std::move(cmd));
    return true;
}

bool TimelineHistory::SetTrackMuted(int track_index, bool muted,
                                    std::string* error_out) {
    auto cmd =
        std::make_unique<SetTrackFlagCommand>(track_index, 'm', muted);
    auto* raw = cmd.get();
    if (!cmd->Apply(project_)) {
        if (error_out) *error_out = raw->last_error();
        return false;
    }
    PushBack(std::move(cmd));
    return true;
}

bool TimelineHistory::SetTrackHidden(int track_index, bool hidden,
                                     std::string* error_out) {
    auto cmd =
        std::make_unique<SetTrackFlagCommand>(track_index, 'h', hidden);
    auto* raw = cmd.get();
    if (!cmd->Apply(project_)) {
        if (error_out) *error_out = raw->last_error();
        return false;
    }
    PushBack(std::move(cmd));
    return true;
}

bool TimelineHistory::SetTrackGainDb(int track_index, float db,
                                     std::string* error_out) {
    auto cmd =
        std::make_unique<SetGainLikeCommand>('g', track_index, db);
    auto* raw = cmd.get();
    if (!cmd->Apply(project_)) {
        if (error_out) *error_out = raw->last_error();
        return false;
    }
    PushBack(std::move(cmd));
    return true;
}

bool TimelineHistory::SetTrackPan(int track_index, float pan,
                                  std::string* error_out) {
    auto cmd =
        std::make_unique<SetGainLikeCommand>('p', track_index, pan);
    auto* raw = cmd.get();
    if (!cmd->Apply(project_)) {
        if (error_out) *error_out = raw->last_error();
        return false;
    }
    PushBack(std::move(cmd));
    return true;
}

bool TimelineHistory::SetMasterGainDb(float db, std::string* error_out) {
    auto cmd = std::make_unique<SetGainLikeCommand>('M', 0, db);
    auto* raw = cmd.get();
    if (!cmd->Apply(project_)) {
        if (error_out) *error_out = raw->last_error();
        return false;
    }
    PushBack(std::move(cmd));
    return true;
}

bool TimelineHistory::SetClipLinkGroup(const std::string& clip_id,
                                       const std::string& group) {
    return Execute(
        std::make_unique<SetClipLinkGroupCommand>(clip_id, group));
}

bool TimelineHistory::SetClipKeyframes(const std::string& clip_id,
                                       KeyProp prop,
                                       std::vector<Keyframe> keys,
                                       std::string* error_out) {
    auto cmd = std::make_unique<SetClipKeyframesCommand>(
        clip_id, prop, std::move(keys));
    auto* raw = cmd.get();
    if (!cmd->Apply(project_)) {
        if (error_out) {
            *error_out = raw->last_error();
        }
        return false;
    }
    PushBack(std::move(cmd));
    return true;
}

}  // namespace XER::VideoEditor
