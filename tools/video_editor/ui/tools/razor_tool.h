// Razor tool (C) — click cuts the clip under the pointer at the
// snapped timeline position (Premiere razor semantics; SplitClip keeps
// the first half under the same id). Locked tracks refuse the cut.
#pragma once

#include "edit_tool.h"

namespace XER::VideoEditor::Ui {

class RazorTool final : public EditTool {
public:
    ToolId Id() const override { return ToolId::Razor; }
    const char* Name() const override { return "Razor"; }
    const char* CursorHint() const override {
        return "click a clip to split it";
    }

    bool OnPointerDown(const ToolContext& ctx,
                       ToolCommit& commit) override;
};

}  // namespace XER::VideoEditor::Ui
