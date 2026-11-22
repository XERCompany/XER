// ui2 shell layout persistence — the "editor config" half of the
// Resizable::SetOnLayout contract (spec §5: sizes persist via
// SetOnLayout → editor config). The shell feeds every Resizable
// SetOnLayout callback into ShellPrefs, marks the state dirty, and
// the app flushes it to disk (debounced) at
//   <user-data>/XER/VideoEditor/ui2_shell.json
// — the same user-data directory the media catalog DB lives in.
//
// The file is ui2's own; the old ui/ shell keeps its (non-persisted)
// in-memory layout — nothing under tools/video_editor/ui/ reads this.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace XER::VideoEditor::Ui2 {

struct ShellPrefs {
    // Last-applied workspace preset name ("" = untracked custom layout).
    std::string workspace;
    bool left_visible = true;
    bool right_visible = true;
    bool status_visible = true;
    bool source_monitor_visible = true;
    bool monitors_tabbed = false;
    int left_tab = 0;
    int right_tab = 0;
    // Resolved split fractions captured from each Resizable's
    // SetOnLayout callback (empty = never laid out — defaults apply).
    std::vector<float> top_sizes;      // [left | monitors | right]
    std::vector<float> outer_sizes;    // [body | timeline | status]
    std::vector<float> monitor_sizes;  // [source | program]
};

// <user-data>/XER/VideoEditor/ui2_shell.json — same env chain the
// catalog's DefaultDbPath() walks (LOCALAPPDATA → XDG_DATA_HOME →
// HOME/.local/share → temp → ".").
std::filesystem::path DefaultShellPrefsPath();

// Load/Save as JSON. Load returns false on missing/malformed files
// (callers keep defaults); Save creates the parent directory and
// writes atomically-ish (truncate+write — same guarantees the
// catalog's own files rely on).
bool LoadShellPrefs(const std::filesystem::path& path, ShellPrefs* out,
                    std::string* error = nullptr);
bool SaveShellPrefs(const std::filesystem::path& path,
                    const ShellPrefs& prefs,
                    std::string* error = nullptr);

}  // namespace XER::VideoEditor::Ui2
