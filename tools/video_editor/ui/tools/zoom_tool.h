// Zoom tool (Z) — click zooms in x1.5 anchored at the pointer's
// timeline position, Alt-click zooms out, and a horizontal drag draws
// a rubber-band marquee whose released range fills the view
// (ToolCommit::ZoomAt / ZoomToRange + SetMarquee).
#pragma once

#include "edit_tool.h"

namespace XER::VideoEditor::Ui {

class ZoomTool final : public EditTool {
public:
    ToolId Id() const override { return ToolId::Zoom; }
    const char* Name() const override { return "Zoom"; }
    const char* CursorHint() const override {
        return "click to zoom in, Alt-click out, drag a range";
    }

    bool OnPointerDown(const ToolContext& ctx,
                       ToolCommit& commit) override;
    bool OnPointerMove(const ToolContext& ctx,
                       ToolCommit& commit) override;
    bool OnPointerUp(const ToolContext& ctx,
                     ToolCommit& commit) override;
    void Cancel(ToolCommit& commit) override;

private:
    float anchor_x_ = 0.0f;
    double anchor_time_ = 0.0;
    bool marquee_ = false;
    bool alt_ = false;
};

}  // namespace XER::VideoEditor::Ui
