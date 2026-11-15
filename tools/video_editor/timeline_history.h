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
    bool SplitClip(const std::string& clip_id, double split_timeline_t,
                   std::string* new_clip_id_out = nullptr,
                   std::string* error_out = nullptr);
    bool ReplaceClip(const std::string& clip_id, const Clip& replacement,
                     const MediaBounds& bounds, std::string* error_out = nullptr);
    bool AddTransition(const std::string& from_clip,
                       const std::string& to_clip, Transition::Type type,
                       double duration, std::string* error_out = nullptr);
    bool RemoveTransition(const std::string& from_clip,
                          const std::string& to_clip);
    int  AddTrack(TrackKind kind, const std::string& name);
    bool RemoveTrack(int track_index, std::string* error_out = nullptr);

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
