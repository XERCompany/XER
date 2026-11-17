// Hand tool (H) — drag pans the timeline view horizontally: the time
// under the pointer stays anchored while scroll_seconds_ follows
// (ToolCommit::PanBy). Consumes every pointer-down inside the widget.
#pragma once

#include "edit_tool.h"

namespace XER::VideoEditor::Ui {

class HandTool final : public EditTool {
public:
    ToolId Id() const override { return ToolId::Hand; }
    const char* Name() const override { return "Hand"; }
    const char* CursorHint() const override {
        return "drag to pan the timeline";
    }

    bool OnPointerDown(const ToolContext& ctx,
                       ToolCommit& commit) override;
    bool OnPointerMove(const ToolContext& ctx,
                       ToolCommit& commit) override;
    bool OnPointerUp(const ToolContext& ctx,
                     ToolCommit& commit) override;

private:
    double anchor_time_ = 0.0;    // timeline seconds under the pointer
};

}  // namespace XER::VideoEditor::Ui
