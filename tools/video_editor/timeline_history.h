// Undo/redo history for the VideoEditor timeline model. Every
// mutation of a Project that should be reversible goes through a
// TimelineHistory convenience wrapper (or Execute with a custom
// Command); the raw model ops in timeline_model.h stay the
// transactional primitives the commands call.
//
// Command granularity mirrors XER::NodeGraph::History: clip edits are
// slice commands that capture the touched clips (by vector index) plus
// a FULL copy of project.transitions before first Apply, and restore
// both on Revert — so a move/trim/split that made the model drop
// transitions brings them back verbatim. Track commands additionally
// snapshot the whole tracks vector.
#pragma once

#include "timeline_model.h"

#include <cstddef>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace XER::VideoEditor {

class Command {
public:
    virtual ~Command() = default;
    virtual bool Apply(Project& project) = 0;
    virtual bool Revert(Project& project) = 0;
    virtual std::string Label() const = 0;
    // Allows coalescing continuous edits (e.g. a drag or a slider drag).
    virtual bool MergeWith(const Command&) { return false; }
};

// Undo/redo stack. All edits made through TimelineHistory are
// reversible; a drag/trim gesture is grouped via BeginMacro/EndMacro
// into a single undo step.
//
// Load contract: this history is bound to ONE Project& and its pending
// commands capture slices of apply-time state. Loading a different
// project into that Project (e.g. ProjectStore::Load) while commands
// are pending lets Undo() restore pre-load clips into the new project
// — call Clear() before the load (or reconstruct history after it).
// Undo history is not migrated across loads.
class TimelineHistory {
public:
    explicit TimelineHistory(Project& project, std::size_t max_depth = 50);
    ~TimelineHistory();
    TimelineHistory(const TimelineHistory&) = delete;
    TimelineHistory& operator=(const TimelineHistory&) = delete;

    bool Execute(std::unique_ptr<Command> command);
    bool Undo();
    bool Redo();
    bool CanUndo() const { return !undo_.empty(); }
    bool CanRedo() const { return !redo_.empty(); }
    std::string UndoLabel() const;
    std::string RedoLabel() const;
    // Full-stack labels for the History panel: undo is oldest→newest
    // (rendered newest-on-top by the panel), redo is next-to-redo first.
    std::vector<std::string> UndoLabels() const;
    std::vector<std::string> RedoLabels() const;
    // Drops undo/redo/macro state. REQUIRED before loading a different
    // project into the bound Project& — see the class comment.
    void Clear();

    // Groups subsequent commands into one undo step. Nestable: ending
    // an inner macro folds it into the enclosing one.
    void BeginMacro(const std::string& label);
    void EndMacro();

    // Convenience wrappers — EVERY model mutation goes through these.
    bool AddClip(Clip clip, const MediaBounds& bounds,
                 std::string* clip_id_out = nullptr,
                 std::string* error_out = nullptr);
    bool RemoveClip(const std::string& clip_id);
    bool MoveClip(const std::string& clip_id, double new_timeline_start,
                  const MediaBounds& bounds, std::string* error_out = nullptr);
    bool TrimClip(const std::string& clip_id, TrimEdge edge,
                  double new_point, const MediaBounds& bounds,
                  std::string* error_out = nullptr);
    // Ripple trim — TrimClip + close the freed gap / push followers
    // on the clip's track, one "Ripple trim" macro. The algorithm
    // the TimelineBridge surface-commit uses, lifted here so
    // app-level verbs (Q/W top-and-tail) share it. `bounds_for`
    // resolves media bounds per source path (the caller's catalog
    // seam) for the trim + follower move validation.
    bool RippleTrimClip(
        const std::string& clip_id, TrimEdge edge, double point,
        const std::function<MediaBounds(const std::string&)>&
            bounds_for,
        std::string* error_out = nullptr);
    bool SplitClip(const std::string& clip_id, double split_timeline_t,
                   std::string* new_clip_id_out = nullptr,
                   std::string* error_out = nullptr);
    bool ReplaceClip(const std::string& clip_id, const Clip& replacement,
                     const MediaBounds& bounds, std::string* error_out = nullptr);
    bool AddTransition(const std::string& from_clip,
                       const std::string& to_clip, Transition::Type type,
                       double duration, std::string* error_out = nullptr,
                       const std::string& script = "",
                       const std::string& xfade = "",
                       const std::string& xer_fx = "",
                       const std::string& xer_params = "");
    bool RemoveTransition(const std::string& from_clip,
                          const std::string& to_clip);
    // Junction-handle drag commit: retargets an existing transition's
    // centered duration (the widget clamps via TransitionDurationLimit
    // before calling). Consecutive sets on the same junction coalesce.
    bool SetTransitionDuration(const std::string& from_clip,
                               const std::string& to_clip,
                               double duration,
                               std::string* error_out = nullptr);
    int  AddTrack(TrackKind kind, const std::string& name);
    bool RemoveTrack(int track_index, std::string* error_out = nullptr);

    // Phase-2 ops — same wrapper contract: rejected edits push no
    // undo entry; clip_id_out mirrors AddClip (empty id gets a
    // generated one).
    bool RippleDeleteClip(const std::string& clip_id,
                          std::string* error_out = nullptr);
    // `track_target` is the optional track-sized targeting mask —
    // empty applies to every unlocked track (model LiftRange parity).
    bool LiftRange(double t0, double t1, bool close_gap,
                   std::string* error_out = nullptr,
                   const std::vector<bool>& track_target = {});
    // Compound (Premiere "Nest"): selected clips move into a new
    // stored Sequence and one compound clip replaces them. Ids are
    // minted internally and returned via the out-params (the model
    // op takes them by value so the snapshot command replays
    // deterministically). FlattenCompound restores the sequence's
    // clips inline — see timeline_model.h for the placement rules.
    bool MakeCompound(std::vector<std::string> clip_ids,
                      std::string name,
                      std::string* clip_id_out = nullptr,
                      std::string* sequence_id_out = nullptr,
                      std::string* error_out = nullptr);
    bool FlattenCompound(const std::string& clip_id,
                         std::string* error_out = nullptr);
    bool RollingEdit(const std::string& from_clip,
                     const std::string& to_clip, double delta_seconds,
                     const MediaBounds& from_bounds,
                     const MediaBounds& to_bounds,
                     std::string* error_out = nullptr);
    bool SlipClip(const std::string& clip_id, double delta_seconds,
                  const MediaBounds& bounds,
                  std::string* error_out = nullptr);
    bool SlideClip(const std::string& clip_id, double delta_seconds,
                   const MediaBounds& bounds,
                   std::string* error_out = nullptr);
    bool SetClipRate(const std::string& clip_id, double rate,
                     const MediaBounds& bounds,
                     std::string* error_out = nullptr);
    bool InsertClipAt(Clip clip, const MediaBounds& bounds,
                      std::string* clip_id_out = nullptr,
                      std::string* error_out = nullptr);
    bool OverwriteClipAt(Clip clip, const MediaBounds& bounds,
                         std::string* clip_id_out = nullptr,
                         std::string* error_out = nullptr);
    bool AddMarker(const Marker& marker,
                   std::string* error_out = nullptr);
    bool RemoveMarker(double time);
    bool SetWorkArea(double in_seconds, double out_seconds,
                     std::string* error_out = nullptr);
    bool SetTrackLocked(int track_index, bool locked,
                        std::string* error_out = nullptr);
    // Mixer-strip edits — undoable, continuous writes coalesce.
    bool SetTrackMuted(int track_index, bool muted,
                       std::string* error_out = nullptr);
    bool SetTrackHidden(int track_index, bool hidden,
                        std::string* error_out = nullptr);
    bool SetTrackGainDb(int track_index, float db,
                        std::string* error_out = nullptr);
    bool SetTrackPan(int track_index, float pan,
                     std::string* error_out = nullptr);
    bool SetMasterGainDb(float db, std::string* error_out = nullptr);
    bool SetClipLinkGroup(const std::string& clip_id,
                          const std::string& group);
    bool SetClipKeyframes(const std::string& clip_id, KeyProp prop,
                          std::vector<Keyframe> keys,
                          std::string* error_out = nullptr);

private:
    class MacroCommand;
    // Post-Apply stack management shared by Execute and the wrappers
    // that must inspect the command after a failed Apply (the command
    // object is destroyed by Execute on failure, so error/id out-params
    // are read while `cmd` is still alive — the NodeGraph AddNode
    // pattern).
    void PushBack(std::unique_ptr<Command> command);

    Project& project_;
    std::size_t max_depth_;
    std::deque<std::unique_ptr<Command>> undo_;
    std::vector<std::unique_ptr<Command>> redo_;
    std::vector<std::unique_ptr<MacroCommand>> macro_stack_;
};

}  // namespace XER::VideoEditor
