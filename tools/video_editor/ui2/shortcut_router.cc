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

bool ShortcutRouter::NormalizeKeyUp(const Event& event, char* key,
                                    KeyCode* code) {
    if (event.type != Event::Type::KeyUp ||
        event.key == KeyCode::None || key == nullptr ||
        code == nullptr) {
        return false;
    }
    *key = 0;
    *code = KeyCode::None;
    // Letter KeyCodes (A..Z are contiguous) normalize to the same
    // `key` char the press's TextInput produced.
    if (event.key >= KeyCode::A && event.key <= KeyCode::Z) {
        *key = static_cast<char>(
            'a' + (static_cast<int>(event.key) -
                   static_cast<int>(KeyCode::A)));
        return true;
    }
    // Held special keys (none bound today — the seam exists for
    // Space/arrows later) keep their KeyCode identity.
    *code = event.key;
    return true;
}

bool ShortcutRouter::HandleEvent(const Event& event) {
    // KeyUp: route the release to the held binding that recorded the
    // press — the gesture's owning binding, not the scope's. An
    // unrecorded release is unhandled (KeyUp is never a shortcut on
    // its own — old parity).
    if (event.type == Event::Type::KeyUp) {
        char key = 0;
        KeyCode code = KeyCode::None;
        if (!NormalizeKeyUp(event, &key, &code)) return false;
        // A letter release carries the TextInput identity of its
        // press (key='k'); a Ctrl-chord press of the same physical
        // key recorded {key=0, code=K} — the release must match
        // either or a chord-held binding would leak.
        const KeyCode raw = event.key;
        for (std::size_t i = 0; i < held_keys_.size(); ++i) {
            const HeldKey& h = held_keys_[i];
            const bool match = (h.key == key && h.code == code) ||
                               (h.key == 0 && h.code == raw);
            if (!match) continue;
            const std::size_t index = h.binding;
            held_keys_.erase(held_keys_.begin() +
                             static_cast<std::ptrdiff_t>(i));
            if (index < bindings_.size() &&
                bindings_[index].on_up != nullptr) {
                return bindings_[index].on_up();
            }
            return true;   // held binding without on_up still eats it
        }
        return false;
    }

    Binding probe;
    if (!Normalize(event, &probe)) return false;
    // Auto-repeat of a held key re-fires the recorded binding — the
    // gesture stays in the scope it started in (the active scope may
    // have moved under the user's mouse mid-hold).
    for (const HeldKey& held : held_keys_) {
        if (held.key == probe.key && held.code == probe.code) {
            if (held.binding < bindings_.size() &&
                bindings_[held.binding].handler != nullptr) {
                bindings_[held.binding].handler();
            }
            return true;
        }
    }
    // Active-scope bindings shadow global ones; a declining handler
    // (returns false) falls through to the next match.
    for (int pass = 0; pass < 2; ++pass) {
        const char* wanted =
            pass == 0 ? active_scope_.c_str() : kScopeGlobal;
        for (std::size_t i = 0; i < bindings_.size(); ++i) {
            const Binding& b = bindings_[i];
            if (b.scope != wanted) continue;
            if (b.key != probe.key || b.code != probe.code ||
                b.ctrl != probe.ctrl || b.shift != probe.shift ||
                b.alt != probe.alt) {
                continue;
            }
            if (b.handler == nullptr || b.handler()) {
                if (b.needs_key_up) {
                    held_keys_.push_back(
                        HeldKey{probe.key, probe.code, i});
                }
                return true;
            }
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
