// Implementation of TimelineHistory — see timeline_history.h for the
// contract. Structure mirrors src/node_graph/node_graph_history.cc;
// the difference is the slice-restore model: clip commands snapshot
// the touched clips (vector index + full Clip) and the ENTIRE
// transitions vector before first Apply, then RestoreSlice puts both
// back verbatim on Revert.

#include "timeline_history.h"

#include <algorithm>
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

    TimelineSlice slice_;
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
    // and adopts the LATEST target.
    bool MergeWith(const Command& other) override {
        const auto* m = dynamic_cast<const MoveClipCommand*>(&other);
        if (m == nullptr || m->id_ != id_) {
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
    // followed by an out-trim is two gestures.
    bool MergeWith(const Command& other) override {
        const auto* t = dynamic_cast<const TrimClipCommand*>(&other);
        if (t == nullptr || t->id_ != id_ || t->edge_ != edge_) {
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
    // captured slice (pre-edit state) is kept for Revert.
    bool MergeWith(const Command& other) override {
        const auto* r = dynamic_cast<const ReplaceClipCommand*>(&other);
        if (r == nullptr || r->id_ != id_) {
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

// Transition commands are dedicated (not slice-based): they mutate
// project.transitions directly and restore the exact Transition.
class AddTransitionCommand : public Command {
public:
    AddTransitionCommand(std::string from, std::string to,
                         Transition::Type type, double duration)
        : from_(std::move(from)),
          to_(std::move(to)),
          type_(type),
          duration_(duration) {}
    bool Apply(Project& project) override {
        last_error_.clear();
        return AddTransition(project, from_, to_, type_, duration_,
                             &last_error_);
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
        for (auto& c : commands_) {
            if (!c->Apply(project)) {
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
                                    std::string* error_out) {
    auto cmd = std::make_unique<AddTransitionCommand>(from_clip, to_clip,
                                                    type, duration);
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

}  // namespace XER::VideoEditor
