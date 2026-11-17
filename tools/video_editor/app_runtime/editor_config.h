// Editor-level config declared by assets: theme/accent/panel defaults
// (assets/editor.yaml) and keyboard-shortcut overrides
// (assets/shortcuts.yaml). Both are optional — absent files leave the
// compiled-in defaults untouched.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>

namespace XER::VideoEditor::AppRuntime {

struct EditorConfig {
    std::string theme;            // "dark" | "light" ("" = keep current)
    std::uint32_t accent_argb = 0;  // 0 = keep theme accent
    int right_tab = -1;           // -1 = keep default tab
    bool left_panel = true;
    bool right_panel = true;
    int timeline_height = 0;      // px, 0 = default layout math
    int waveform_height = 0;
    std::string media_bin_view;   // "grid" | "list" ("" = grid)
    bool show_status_bar = true;
};

// assets/editor.yaml:
//   Editor:
//     Theme: dark
//     Accent: "#8B5CF6"         # AARRGGBB or #RRGGBB
//     RightTab: 0
//     Panels: {Left: true, Right: true}
//     Timeline: {Height: 220}
//     Waveform: {Height: 72}
//     MediaBin: {View: grid}
//     StatusBar: true
bool LoadEditorConfig(const std::filesystem::path& yaml_path,
                      EditorConfig* out, std::string* error);

struct ShortcutsConfig {
    // action id → combo string ("Ctrl+Z", "Shift+Wheel", "Space", ...)
    std::unordered_map<std::string, std::string> keys;
};

// assets/shortcuts.yaml:
//   Shortcuts:
//     playback.toggle: Space
//     edit.undo: Ctrl+Z
bool LoadShortcutsConfig(const std::filesystem::path& yaml_path,
                         ShortcutsConfig* out, std::string* error);

}  // namespace XER::VideoEditor::AppRuntime
