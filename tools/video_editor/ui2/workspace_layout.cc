#include "workspace_layout.h"

namespace XER::VideoEditor::Ui2 {

const std::vector<WorkspaceLayout>& BuiltinWorkspaces() {
    static const std::vector<WorkspaceLayout> kWorkspaces = {
        // Default split: every column up, Inspector active.
        {"Editing", /*left_visible=*/true, /*right_visible=*/true,
         kLeftTabMedia, kRightTabInspector, /*monitors_tabbed=*/false,
         /*timeline_fraction=*/0.32},
        // Premiere "Assembly"-ish: big timeline, bin kept, no right
        // column — for rough-cut/clip-shuffling passes.
        {"Assembly", true, false, kLeftTabMedia, kRightTabInspector,
         false, 0.45},
        // Effects pass: effects browser left, FX-focused right tab
        // (M1 maps it to Inspector — the dedicated FX inspector tab
        // lands with the inspector panel in M3).
        {"Effects", true, true, kLeftTabEffects, kRightTabInspector,
         false, 0.30},
        // Color pass: Color tab up + compact timeline so the monitor
        // keeps the most room.
        {"Color", true, true, kLeftTabMedia, kRightTabColor, false,
         0.30},
        // Audio pass: mixer tab, bin hidden — strips need the column.
        {"Audio", false, true, kLeftTabAudio, kRightTabMix, false,
         0.30},
        // Marker review: markers tab + a taller timeline for the
        // marker lane.
        {"Markers", true, true, kLeftTabMedia, kRightTabMarkers, false,
         0.38},
        // Single-viewer small-window preset (CapCut/Resolve-Cut
        // pattern): tabbed monitors, bin hidden, big timeline.
        {"Cut", false, true, kLeftTabMedia, kRightTabInspector, true,
         0.42},
        // Photo editing: the PhotoWorkspace band replaces the monitor
        // region (tools column + canvas + layers/adjust docks), both
        // shell docks and the timeline collapse — the band brings its
        // own panels.
        {"Photo", false, false, kLeftTabMedia, kRightTabInspector,
         false, 0.0},
    };
    return kWorkspaces;
}

const WorkspaceLayout* FindWorkspace(const std::string& name) {
    for (const WorkspaceLayout& ws : BuiltinWorkspaces()) {
        if (ws.name == name) return &ws;
    }
    return nullptr;
}

}  // namespace XER::VideoEditor::Ui2
