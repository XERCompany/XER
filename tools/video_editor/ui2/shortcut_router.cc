#include "shortcut_router.h"

#include <cctype>

namespace XER::VideoEditor::Ui2 {

bool ShortcutRouter::Normalize(const Event& event, Binding* out) {
    if (out == nullptr) return false;
    if (event.type == Event::Type::TextInput && event.text.size() == 1 &&
        !event.is_ctrl) {
        // Unmodified/Shift/Alt printable keys — J/K/L, I/O, V/C/B/N/Y,
        // M, Alt+I… Ctrl+letters never arrive here (the platform
        // translator turns them into KeyDown chords).
        out->key = static_cast<char>(
            std::tolower(static_cast<unsigned char>(event.text.front())));
        out->shift = event.is_shift;
        out->alt = event.is_alt;
        return true;
    }
    if (event.type == Event::Type::KeyDown &&
        event.key != KeyCode::None) {
        // Special keys (Space, arrows, Home/End, PageUp/Down, Delete,
        // Escape, F5) and Ctrl+letter chords (KeyCode::S + is_ctrl).
        out->code = event.key;
        out->ctrl = event.is_ctrl;
        out->shift = event.is_shift;
        out->alt = event.is_alt;
        return true;
    }
    return false;
}

bool ShortcutRouter::HandleEvent(const Event& event) {
    Binding probe;
    if (!Normalize(event, &probe)) return false;
    // Active-scope bindings shadow global ones; a declining handler
    // (returns false) falls through to the next match.
    for (int pass = 0; pass < 2; ++pass) {
        const char* wanted =
            pass == 0 ? active_scope_.c_str() : kScopeGlobal;
        for (const Binding& b : bindings_) {
            if (b.scope != wanted) continue;
            if (b.key != probe.key || b.code != probe.code ||
                b.ctrl != probe.ctrl || b.shift != probe.shift ||
                b.alt != probe.alt) {
                continue;
            }
            if (b.handler == nullptr || b.handler()) return true;
        }
    }
    return false;
}

bool ShortcutRouter::IsBound(const Event& event) const {
    Binding probe;
    if (!Normalize(event, &probe)) return false;
    for (const Binding& b : bindings_) {
        if ((b.scope == active_scope_ || b.scope == kScopeGlobal) &&
            b.key == probe.key && b.code == probe.code &&
            b.ctrl == probe.ctrl && b.shift == probe.shift &&
            b.alt == probe.alt) {
            return true;
        }
    }
    return false;
}

}  // namespace XER::VideoEditor::Ui2
