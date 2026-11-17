// Rolling-edit tool (N) — drags the cut between two ADJACENT clips on
// one track: `from` grows/shrinks out of its out-handle while `to`
// gives up/extends head material, so the pair's combined span stays
// fixed. Commits ToolCommit::RollingEdit(from_id, to_id, delta) with
// delta in timeline seconds. Locked tracks refuse the gesture.
#pragma once

#include <string>

#include "edit_tool.h"

namespace XER::VideoEditor::Ui {

class RollingEditTool final : public EditTool {
public:
    ToolId Id() const override { return ToolId::RollingEdit; }
    const char* Name() const override { return "Rolling Edit"; }
    const char* CursorHint() const override {
        return "drag a shared cut to roll it";
    }

    bool OnPointerDown(const ToolContext& ctx,
                       ToolCommit& commit) override;
    bool OnPointerMove(const ToolContext& ctx,
                       ToolCommit& commit) override;
    bool OnPointerUp(const ToolContext& ctx,
                     ToolCommit& commit) override;
    void Cancel(ToolCommit& commit) override;

private:
    std::string from_id_;
    std::string to_id_;
    int track_ = -1;
    double cut_ = 0.0;            // original cut position (timeline s)
    double from_start_ = 0.0;
    double to_end_ = 0.0;
    double from_duration_ = 0.0;  // clamps below keep both sides valid
    double to_duration_ = 0.0;
    double delta_ = 0.0;
};

}  // namespace XER::VideoEditor::Ui
