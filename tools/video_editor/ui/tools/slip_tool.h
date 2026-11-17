// Slip tool (Y) — body drag shifts the clip's source window
// (in_point += delta SOURCE seconds) while its timeline position and
// duration stay fixed. The live status reports the slipped in/out
// source times; the commit is ToolCommit::SlipClip(id, delta).
#pragma once

#include <string>

#include "edit_tool.h"

namespace XER::VideoEditor::Ui {

class SlipTool final : public EditTool {
public:
    ToolId Id() const override { return ToolId::Slip; }
    const char* Name() const override { return "Slip"; }
    const char* CursorHint() const override {
        return "drag a clip body to slip its source";
    }

    bool OnPointerDown(const ToolContext& ctx,
                       ToolCommit& commit) override;
    bool OnPointerMove(const ToolContext& ctx,
                       ToolCommit& commit) override;
    bool OnPointerUp(const ToolContext& ctx,
                     ToolCommit& commit) override;
    void Cancel(ToolCommit& commit) override;

private:
    std::string clip_id_;
    double down_time_ = 0.0;      // ctx.time at pointer-down
    double orig_in_ = 0.0;
    double orig_duration_ = 0.0;
    double orig_rate_ = 1.0;
    double orig_start_ = 0.0;
    double delta_ = 0.0;          // accumulated source-seconds delta
};

}  // namespace XER::VideoEditor::Ui
