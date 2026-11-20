// Workspace presets: Premiere-style named panel layouts ("Editing",
// "Effects", "Color", "Audio", ...). Pure data — the host applies a
// preset by copying the fields onto its own layout state:
//   left_visible       -> media-bin column visibility
//   right_visible      -> right-column tab group visibility
//   right_tab          -> active right-column tab, the same index the
//                         tab strip uses (0..9):
//                           0 Insp · 1 FX · 2 Mkr · 3 Lab · 4 Hist ·
//                           5 Mix · 6 Queue · 7 Plugins · 8 Color · 9 Web
//   timeline_fraction  -> timeline height as a fraction of the
//                         vertical content area (0.32 matches the
//                         app's default editing split; the host
//                         clamps to its own min/max pixel range)
#pragma once

#include <string>
#include <vector>

namespace XER::VideoEditor::Ui {

struct WorkspaceLayout {
    std::string name;
    bool left_visible = true;
    bool right_visible = true;
    int right_tab = 0;
    double timeline_fraction = 0.32;
};

// Built-in presets in menu order. Returned by const reference — the
// list is process-static and must be treated as read-only.
const std::vector<WorkspaceLayout>& BuiltinWorkspaces();

}  // namespace XER::VideoEditor::Ui
