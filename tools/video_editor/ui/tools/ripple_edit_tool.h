// Ripple-edit tool (B) — Premiere ripple trim: dragging a clip edge
// trims it AND closes/pushes the gap on its track (the committed op is
// ToolCommit::RippleTrim -> Hooks::on_ripple_trim, one undo entry in
// the app shell; falls back to a plain trim when the hook is unset).
// Body clicks only select — the gesture starts on the ~7 px edges.
#pragma once

#include <string>

#include "edit_tool.h"

namespace XER::VideoEditor::Ui {

class RippleEditTool final : public EditTool {
public:
    ToolId Id() const override { return ToolId::RippleEdit; }
    const char* Name() const override { return "Ripple Edit"; }
    const char* CursorHint() const override {
        return "drag a clip edge to ripple-trim";
    }

    bool OnPointerDown(const ToolContext& ctx,
                       ToolCommit& commit) override;
    bool OnPointerMove(const ToolContext& ctx,
                       ToolCommit& commit) override;
    bool OnPointerUp(const ToolContext& ctx,
                     ToolCommit& commit) override;
    void Cancel(ToolCommit& commit) override;

private:
    enum class Drag { None, TrimIn, TrimOut };
    Drag drag_ = Drag::None;
    std::string clip_id_;
    int track_ = -1;
    double orig_start_ = 0.0;
    double orig_in_ = 0.0;
    double orig_duration_ = 0.0;
    double orig_rate_ = 1.0;
    double preview_point_ = 0.0;
};

}  // namespace XER::VideoEditor::Ui
