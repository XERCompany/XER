#include "hand_tool.h"

namespace XER::VideoEditor::Ui {

bool HandTool::OnPointerDown(const ToolContext& ctx,
                             ToolCommit& commit) {
    (void)commit;
    dragging_ = true;
    anchor_time_ = ctx.time;
    return true;
}

bool HandTool::OnPointerMove(const ToolContext& ctx,
                             ToolCommit& commit) {
    if (!dragging_) return false;
    // Keep the grabbed time under the pointer: ctx.time already
    // reflects the current scroll, so the correction is the residual
    // gap (the gesture converges in a single step).
    commit.PanBy(anchor_time_ - ctx.time);
    return true;
}

bool HandTool::OnPointerUp(const ToolContext& ctx,
                           ToolCommit& commit) {
    (void)ctx;
    (void)commit;
    if (!dragging_) return false;
    dragging_ = false;
    return true;
}

}  // namespace XER::VideoEditor::Ui
