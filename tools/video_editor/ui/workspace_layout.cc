#include "workspace_layout.h"

namespace XER::VideoEditor::Ui {

const std::vector<WorkspaceLayout>& BuiltinWorkspaces() {
    static const std::vector<WorkspaceLayout> kWorkspaces = {
        // Default split: every column up, Inspector active.
        {"Editing", /*left_visible=*/true, /*right_visible=*/true,
         /*right_tab=*/0, /*timeline_fraction=*/0.32},
        // Premiere "Assembly"-ish: big timeline, bin kept, no right
        // column — for rough-cut/clip-shuffling passes.
        {"Assembly", true, false, 0, 0.45},
        // Effects pass: FX panel focused, slightly taller timeline.
        {"Effects", true, true, 1, 0.30},
        // Color pass: Inspector (lumetri-style controls) + compact
        // timeline so the monitor keeps the most room.
        {"Color", true, true, 0, 0.30},
        // Audio pass: mixer tab, bin hidden — strips need the column.
        {"Audio", false, true, 5, 0.30},
        // Marker review: markers tab + a taller timeline for the
        // marker lane.
        {"Markers", true, true, 2, 0.38},
        // Lab experiments: lab tab up, editing split otherwise.
        {"Lab", true, true, 3, 0.32},
    };
    return kWorkspaces;
}

}  // namespace XER::VideoEditor::Ui
