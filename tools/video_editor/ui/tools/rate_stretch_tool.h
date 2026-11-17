// Rate-stretch tool (R) — Premiere rate stretch: dragging a clip edge
// retimes the clip so its consumed SOURCE span stays fixed while the
// timeline duration flexes (SetClipRate semantics: rate =
// consumed / new_span). The live preview resizes the clip and reports
// the rate as a multiplier; the commit is
// ToolCommit::SetClipRate(id, new_rate).
#pragma once

#include <string>

#include "edit_tool.h"

namespace XER::VideoEditor::Ui {

class RateStretchTool final : public EditTool {
public:
    ToolId Id() const override { return ToolId::RateStretch; }
    const char* Name() const override { return "Rate Stretch"; }
    const char* CursorHint() const override {
        return "drag a clip edge to retime it";
    }

    bool OnPointerDown(const ToolContext& ctx,
                       ToolCommit& commit) override;
    bool OnPointerMove(const ToolContext& ctx,
                       ToolCommit& commit) override;
    bool OnPointerUp(const ToolContext& ctx,
                     ToolCommit& commit) override;
    void Cancel(ToolCommit& commit) override;

private:
    enum class Drag { None, EdgeIn, EdgeOut };
    Drag drag_ = Drag::None;
    std::string clip_id_;
    int track_ = -1;
    double orig_start_ = 0.0;
    double orig_duration_ = 0.0;
    double orig_rate_ = 1.0;
    double preview_rate_ = 1.0;
};

}  // namespace XER::VideoEditor::Ui
