// Data-driven keyboard-shortcut table for the VideoEditor — the
// single source of truth shared by the shortcuts overlay (? key) and
// anything else that wants to enumerate the bindings, replacing
// hand-mirrored lists. UI-free on purpose: the app-side dispatch
// lives in ui/video_editor_app.cc HandleEvent, this is just the
// human-readable inventory of it.
#pragma once

#include <string>
#include <vector>

namespace XER::VideoEditor {

// One shortcut row: the panel group it is displayed under ("Tools",
// "Playback", ...), the key chord as the user types it ("Ctrl+Z",
// "Left / Right"), and what it does.
struct ShortcutEntry {
    std::string group;
    std::string keys;
    std::string action;
};

// Every binding VideoEditorAppWidget::HandleEvent dispatches, plus the
// menu-advertised chords (Alt+Del ripple delete), ordered by display
// group — Tools, Playback, Markers, Edit, View. The returned vector is
// a static table: stable for the process lifetime, do not mutate.
const std::vector<ShortcutEntry>& EditorShortcuts();

}  // namespace XER::VideoEditor
