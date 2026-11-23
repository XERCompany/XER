// ui2 global shortcut router — the window-level keyboard contract
// (spec §5/G7). Ported from VideoEditorAppWidget::InstallGlobalShortcuts
// + HandleEvent: the shared KeyCode enum only reports letters while
// Ctrl is held, so unmodified Premiere-style keys (J/K/L, I/O, V/C/B/N/Y,
// M, Space…) arrive as Event::Type::TextInput — the router normalizes
// BOTH channels into one binding space:
//
//   TextInput "j" (no ctrl/alt)  -> key 'j', mods = shift
//   KeyDown KeyCode::Space       -> special key
//   KeyDown KeyCode::S + ctrl    -> Ctrl+S chord
//   TextInput "i" + alt          -> Alt+I
//
// Install once on the app's WidgetTree:
//
//   GetTree()->SetKeyHandler([this](const Widgets::Event& e) {
//       return shortcuts_.HandleEvent(e);
//   });
//
// Dispatch order is the kit's, not ours: the tree first offers the
// event to the focused widget, then to a visible overlay; the key
// handler only runs when NEITHER consumed it AND no modal overlay is
// active (widget_tree.cc). Text fields therefore never trigger J/K/L,
// and modal dialogs/dialog palettes swallow everything — preserving
// the old shell's guarantees.
//
// Scopes implement the last-active-viewer rule: bindings live under a
// named scope ("program"/"source"); ActivateScope() — wired to
// MonitorHooks::on_activate — picks which scope-specific binding fires.
// Scope-specific bindings shadow the global scope; a binding that
// returns false declines the event and the lookup falls through to
// the next matching binding (e.g. "Alt+I only while a source is
// loaded" declining leaves the event unhandled).
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "widgets/widget.h"

namespace XER::VideoEditor::Ui2 {

class ShortcutRouter {
public:
    using Event = XER::UI::Widgets::Event;
    using KeyCode = XER::UI::Widgets::KeyCode;
    // Returning false declines the event — dispatch continues to the
    // next matching binding; returning true consumes it.
    using Handler = std::function<bool()>;

    // Well-known scopes. Global bindings match in every scope and act
    // as the fallback when the active scope has no (accepting) binding.
    static constexpr const char* kScopeGlobal = "*";
    static constexpr const char* kScopeProgram = "program";
    static constexpr const char* kScopeSource = "source";

    // Normalized shortcut identity: `key` is the lowercase ASCII char
    // for printable bindings ('j','i','m'…) or 0 when `code` carries a
    // KeyCode (special keys and Ctrl+letter chords).
    struct Binding {
        char key = 0;             // lowercase printable key, or 0
        KeyCode code = KeyCode::None;  // special key / ctrl-letter
        bool ctrl = false;
        bool shift = false;
        bool alt = false;
        std::string scope = kScopeGlobal;
        std::string label;        // menu/palette display name
        Handler handler;
    };

    // Unmodified (or Shift/Alt-modified) printable key — 'j', 'i',
    // 'v'… The text channel reports the produced character, so a
    // binding for 'm' also catches Caps-Lock 'M' (old shell parity).
    void BindKey(char key, Handler handler,
                 std::string scope = kScopeGlobal,
                 std::string label = {}) {
        Binding b;
        b.key = static_cast<char>(
            std::tolower(static_cast<unsigned char>(key)));
        b.scope = std::move(scope);
        b.label = std::move(label);
        b.handler = std::move(handler);
        bindings_.push_back(std::move(b));
    }
    void BindShiftKey(char key, Handler handler,
                      std::string scope = kScopeGlobal,
                      std::string label = {}) {
        BindKey(key, std::move(handler), scope, label);
        bindings_.back().shift = true;
    }
    void BindAltKey(char key, Handler handler,
                    std::string scope = kScopeGlobal,
                    std::string label = {}) {
        BindKey(key, std::move(handler), scope, label);
        bindings_.back().alt = true;
    }

    // Special key or chord: KeyCode::Space, ArrowLeft, Home…, or the
    // Ctrl-letter KeyCodes (KeyCode::S + ctrl=true == Ctrl+S).
    void BindChord(KeyCode code, Handler handler,
                   bool ctrl = false, bool shift = false,
                   bool alt = false,
                   std::string scope = kScopeGlobal,
                   std::string label = {}) {
        Binding b;
        b.code = code;
        b.ctrl = ctrl;
        b.shift = shift;
        b.alt = alt;
        b.scope = std::move(scope);
        b.label = std::move(label);
        b.handler = std::move(handler);
        bindings_.push_back(std::move(b));
    }

    void ClearBindings() { bindings_.clear(); }

    // Last-active-viewer routing — MonitorHooks::on_activate calls this
    // with kScopeSource/kScopeProgram so J/K/L etc. follow the viewer.
    void ActivateScope(std::string scope) { active_scope_ = std::move(scope); }
    const std::string& ActiveScope() const { return active_scope_; }

    // Normalizes `event` and runs the first accepting binding —
    // active-scope bindings shadow global ones. Returns true when the
    // event was consumed. KeyUp is never a shortcut (old parity).
    bool HandleEvent(const Event& event);

    // True when `event` normalizes to any registered binding — the
    // smoke harness uses it to assert dispatch coverage without
    // firing handlers.
    bool IsBound(const Event& event) const;

    const std::vector<Binding>& Bindings() const { return bindings_; }

private:
    // Normalizes the event into (key|code, mods); false when the event
    // cannot be a shortcut (KeyUp, multi-byte IME text, mouse events).
    static bool Normalize(const Event& event, Binding* out);

    std::vector<Binding> bindings_;
    std::string active_scope_ = kScopeProgram;
};

}  // namespace XER::VideoEditor::Ui2
