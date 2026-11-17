// Slide tool (U) — body drag moves the clip along its track while the
// adjacent neighbours absorb the shift (prev out-point / next in-point
// stretch and shrink, model SlideClip semantics). The commit is
// ToolCommit::SlideClip(id, delta) with delta in TIMELINE seconds.
#pragma once

#include <string>

#include "edit_tool.h"

namespace XER::VideoEditor::Ui {

class SlideTool final : public EditTool {
public:
    ToolId Id() const override { return ToolId::Slide; }
    const char* Name() const override { return "Slide"; }
    const char* CursorHint() const override {
        return "drag a clip body to slide it";
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
    int track_ = -1;
    double orig_start_ = 0.0;
    double orig_duration_ = 0.0;
    double grab_seconds_ = 0.0;   // ctx.time - clip.start at pointer-down
    double delta_ = 0.0;
};

}  // namespace XER::VideoEditor::Ui
