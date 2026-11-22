// Workspace presets: Premiere-style named panel layouts ("Editing",
// "Color", "Audio", "Assembly", ...). Pure data — the shell applies a
// preset by copying the fields onto its own layout state:
//
//   left_visible       -> left dock tab-group column visibility
//   right_visible      -> right dock tab-group column visibility
//   left_tab           -> active left-dock tab (0..4):
//                           0 Media · 1 Effects · 2 Audio ·
//                           3 Text · 4 Transitions
//   right_tab          -> active right-dock tab (0..8):
//                           0 Inspector · 1 Color · 2 Markers ·
//                           3 History · 4 Mix · 5 Queue · 6 Ext ·
//                           7 Scopes · 8 Trn
//   monitors_tabbed    -> monitors region shows tabbed single-viewer
//                         (Kdenlive/CapCut mode) instead of the
//                         Resizable Source|Program pair (M1 keeps this
//                         as a stored preference — the shell honors it
//                         when the tabbed-monitor option is wired)
//   timeline_fraction  -> timeline height as a fraction of the vertical
//                         content area below the title bar (0.32 is the
//                         default editing split; the host clamps to its
//                         own min pixel range)
//
// Contract ported verbatim-in-spirit from ui/workspace_layout.h —
// the same field set plus left_tab/monitors_tabbed for the two-dock
// shell. Right-dock tab indices follow the ui2 tab order below (the
// old shell's right-column indices differ — see old
// ui/workspace_layout.cc's SetRightTab call sites for the mapping).
#pragma once

#include <string>
#include <vector>

namespace XER::VideoEditor::Ui2 {

// Left dock tab indices — must match ve_app.cc's AddTab order.
inline constexpr int kLeftTabMedia = 0;
inline constexpr int kLeftTabEffects = 1;
inline constexpr int kLeftTabAudio = 2;
inline constexpr int kLeftTabText = 3;
inline constexpr int kLeftTabTransitions = 4;
inline constexpr int kLeftTabCount = 5;

// Right dock tab indices — must match ve_app.cc's AddTab order.
inline constexpr int kRightTabInspector = 0;
inline constexpr int kRightTabColor = 1;
inline constexpr int kRightTabMarkers = 2;
inline constexpr int kRightTabHistory = 3;
inline constexpr int kRightTabMix = 4;
inline constexpr int kRightTabQueue = 5;
inline constexpr int kRightTabExtensions = 6;
inline constexpr int kRightTabScopes = 7;
inline constexpr int kRightTabTranscript = 8;
inline constexpr int kRightTabCount = 9;

struct WorkspaceLayout {
    std::string name;
    bool left_visible = true;
    bool right_visible = true;
    int left_tab = kLeftTabMedia;
    int right_tab = kRightTabInspector;
    bool monitors_tabbed = false;
    double timeline_fraction = 0.32;
};

// Built-in presets in menu order. Returned by const reference — the
// list is process-static and must be treated as read-only.
const std::vector<WorkspaceLayout>& BuiltinWorkspaces();

// Looks up a built-in preset by name (case-sensitive). nullptr on miss.
const WorkspaceLayout* FindWorkspace(const std::string& name);

}  // namespace XER::VideoEditor::Ui2
