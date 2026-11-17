// Selection tool (V) — the default Premiere timeline tool: click
// selects, body drag moves, ~7 px edge zones trim the in/out point.
// Clips in a non-empty link_group co-move/co-trim with the dragged
// clip (one commit per member — the app wraps them in a single undo
// macro). Locked tracks refuse the drag and report "track is locked".
#pragma once

#include <string>
#include <vector>

#include "edit_tool.h"

namespace XER::VideoEditor::Ui {

class SelectionTool final : public EditTool {
public:
    ToolId Id() const override { return ToolId::Selection; }
    const char* Name() const override { return "Selection"; }
    const char* CursorHint() const override {
        return "select / drag to move, edges to trim";
    }

    bool OnPointerDown(const ToolContext& ctx,
                       ToolCommit& commit) override;
    bool OnPointerMove(const ToolContext& ctx,
                       ToolCommit& commit) override;
    bool OnPointerUp(const ToolContext& ctx,
                     ToolCommit& commit) override;
    void Cancel(ToolCommit& commit) override;

private:
    enum class Drag { None, Move, TrimIn, TrimOut };
    // One linked co-edit member (the drag clip keeps its own fields).
    struct Member {
        std::string id;
        double start = 0.0;
        double in_point = 0.0;
        double duration = 0.0;
        double rate = 1.0;
    };

    Drag drag_ = Drag::None;
    std::string clip_id_;
    int track_ = -1;
    double grab_seconds_ = 0.0;   // ctx.time - clip.start at pointer-down
    double orig_start_ = 0.0;
    double orig_in_ = 0.0;
    double orig_duration_ = 0.0;
    double orig_rate_ = 1.0;
    double preview_point_ = 0.0;  // last previewed source point / start
    std::vector<Member> members_;
};

}  // namespace XER::VideoEditor::Ui
