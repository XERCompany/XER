// Shortcut table — mirrors ui/video_editor_app.cc HandleEvent and the
// title-bar menu labels; keep both in sync when bindings change.
#include "keyboard_map.h"

namespace XER::VideoEditor {

const std::vector<ShortcutEntry>& EditorShortcuts() {
    // Verified against VideoEditorAppWidget::HandleEvent
    // (ui/video_editor_app.cc): single-letter tool/marker/transport
    // keys arrive as TextInput events, chords as KeyDown.
    static const std::vector<ShortcutEntry> kShortcuts = {
        // Premiere-style single-letter tool selection (TextInput).
        {"Tools", "V", "Selection tool"},
        {"Tools", "C", "Razor tool"},
        {"Tools", "B", "Ripple edit tool"},
        {"Tools", "N", "Rolling edit tool"},
        {"Tools", "Y", "Slip tool"},
        {"Tools", "U", "Slide tool"},
        {"Tools", "R", "Rate stretch tool"},
        {"Tools", "H", "Hand tool"},
        {"Tools", "Z", "Zoom tool"},
        // Transport: Space toggles, J/L seek 5 s, K pauses only.
        {"Playback", "Space", "Play / pause"},
        {"Playback", "J", "Seek back 5 s"},
        {"Playback", "K", "Pause"},
        {"Playback", "L", "Seek forward 5 s"},
        {"Playback", "Left / Right", "Step frame (Shift: 5 s)"},
        {"Playback", "Home", "Go to start"},
        {"Playback", "End", "Go to end"},
        // I/O set the work-area (export/preview range), M drops a
        // named marker.
        {"Markers", "M", "Add marker at playhead"},
        {"Markers", "I", "Set work-area in"},
        {"Markers", "O", "Set work-area out"},
        // Ctrl chords are KeyDown; Alt+Del and '?' are advertised by
        // the Edit/View menus.
        {"Edit", "Ctrl+Z", "Undo"},
        {"Edit", "Ctrl+Y", "Redo (also Ctrl+Shift+Z)"},
        {"Edit", "Ctrl+S", "Save (Shift: save as)"},
        {"Edit", "Ctrl+O", "Open project"},
        {"Edit", "Ctrl+N", "New project"},
        {"Edit", "Ctrl+P", "Export MP4"},
        {"Edit", "Del", "Delete clip"},
        {"Edit", "Alt+Del", "Ripple-delete clip"},
        {"Edit", "S", "Toggle snapping"},
        {"View", "?", "Toggle shortcuts overlay"},
    };
    return kShortcuts;
}

}  // namespace XER::VideoEditor
