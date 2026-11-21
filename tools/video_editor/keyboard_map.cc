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
        // Transport: Space toggles at 1x; JKL shuttle on the ±1/2/4/8x
        // ladder — the opposite key steps back down and flips at 1x,
        // K stops.
        {"Playback", "Space", "Play / pause"},
        {"Playback", "J", "Shuttle reverse (repeat: faster)"},
        {"Playback", "K", "Pause / stop shuttle"},
        {"Playback", "L", "Shuttle forward (repeat: faster)"},
        {"Playback", "Left / Right", "Step frame (Shift: 5 s)"},
        {"Playback", "Home", "Go to start"},
        {"Playback", "End", "Go to end"},
        // I/O set the work-area (export/preview range) when the
        // program monitor is active, or the source clip's in/out when
        // the source monitor is. M drops a named marker.
        {"Markers", "M", "Add marker at playhead"},
        {"Markers", "Shift+M", "Go to next marker"},
        {"Markers", "PageUp / PageDown", "Previous / next marker"},
        {"Markers", "I", "Set work-area in / source mark-in"},
        {"Markers", "O", "Set work-area out / source mark-out"},
        // Source monitor (Premiere): keys land on the last-used
        // monitor — transport, marks and the ,/. three-point edits
        // above apply to the loaded source clip while it is active.
        {"Source Monitor", "Alt+I", "Clear source mark-in"},
        {"Source Monitor", "Alt+O", "Clear source mark-out"},
        {"Source Monitor", ",", "Insert edit at playhead"},
        {"Source Monitor", ".", "Overwrite edit at playhead"},
        // Ctrl chords are KeyDown; Alt+Del and '?' are advertised by
        // the Edit/View menus.
        {"Edit", "Ctrl+Z", "Undo"},
        {"Edit", "Ctrl+Y", "Redo (also Ctrl+Shift+Z)"},
        {"Edit", "Ctrl+S", "Save (Shift: save as)"},
        {"Edit", "Ctrl+O", "Open project"},
        {"Edit", "Ctrl+N", "New project"},
        {"Edit", "Ctrl+P", "Export MP4"},
        {"Edit", "Ctrl+D", "Default transition at junction"},
        {"Edit", "Del", "Delete clip"},
        {"Edit", "Alt+Del", "Ripple-delete clip"},
        // Premiere trim/nudge keys — TextInput like the tool letters.
        {"Edit", "Q", "Ripple trim clip head to playhead"},
        {"Edit", "W", "Ripple trim clip tail to playhead"},
        {"Edit", ", / .", "Nudge selection ±1 frame (program active)"},
        {"Edit", "S", "Toggle snapping"},
        {"View", "?", "Toggle shortcuts overlay"},
    };
    return kShortcuts;
}

}  // namespace XER::VideoEditor
