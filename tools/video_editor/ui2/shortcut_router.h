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
//
// Held-key bindings (BindHeldKey) extend the contract to KeyUp: a
// binding that declares needs_key_up is recorded in held_keys_ the
// moment its handler accepts the press, OS auto-repeat presses while
// it is held re-fire the SAME recorded binding (the gesture keeps the
// scope it started in), and the release routes to that binding's
// on_up handler even if the active scope moved meanwhile. This is
// what Premiere's K+J/K+L held combos need: J/L presses see
// IsHeld('k') and run a held slow shuttle instead of the ladder.
#pragma once

#include <cctype>
#include <cstddef>
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
        // Held-key contract: while the key is held the router records
        // this binding (auto-repeat presses re-fire `handler`, never
        // re-dispatch) and the KeyUp release invokes `on_up` — always
        // through THIS binding, regardless of the scope that is active
        // at release time. See BindHeldKey.
        bool needs_key_up = false;
        Handler on_up;
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

    // Held-key binding: `handler` fires on the press (and on each OS
    // auto-repeat while the key stays held — same cadence today's
    // BindKey repeats produce); `on_up` fires once on release. The
    // press arrives through the normal channels (TextInput for
    // unmodified letters, KeyDown for chords); the release arrives as
    // KeyUp — letter KeyCodes normalize back to `key`. While the key
    // is held, IsHeld(key) reports true; other handlers use that for
    // held-combo branching (K held + J/L = slow shuttle, not a ladder
    // step). An unset on_up still consumes the release.
    void BindHeldKey(char key, Handler handler, Handler on_up,
                     std::string scope = kScopeGlobal,
                     std::string label = {}) {
        BindKey(key, std::move(handler), scope, label);
        bindings_.back().needs_key_up = true;
        bindings_.back().on_up = std::move(on_up);
    }

    // True while a held-key binding recorded `key` as pressed.
    bool IsHeld(char key) const {
        key = static_cast<char>(
            std::tolower(static_cast<unsigned char>(key)));
        for (const HeldKey& held : held_keys_) {
            if (held.key == key && held.code == KeyCode::None) {
                return true;
            }
        }
        return false;
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

    void ClearBindings() {
        bindings_.clear();
        held_keys_.clear();
    }

    // Last-active-viewer routing — MonitorHooks::on_activate calls this
    // with kScopeSource/kScopeProgram so J/K/L etc. follow the viewer.
    void ActivateScope(std::string scope) { active_scope_ = std::move(scope); }
    const std::string& ActiveScope() const { return active_scope_; }

    // Normalizes `event` and runs the first accepting binding —
    // active-scope bindings shadow global ones. Returns true when the
    // event was consumed. KeyUp reaches only held-key bindings
    // (recorded at their press); an unrecorded release is unhandled.
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
    // KeyUp identity: a letter KeyCode maps back to its lowercase
    // `key` char (the press arrived as TextInput); any other KeyCode
    // maps to `code` like KeyDown. False only for None/non-KeyUp.
    static bool NormalizeKeyUp(const Event& event, char* key,
                               KeyCode* code);

    // A held-key binding recorded at press: the (key|code) identity
    // plus the binding index that accepted it — auto-repeat re-fires
    // and the release routes here, not through the scope dispatch.
    struct HeldKey {
        char key = 0;
        KeyCode code = KeyCode::None;
        std::size_t binding = 0;
    };

    std::vector<Binding> bindings_;
    std::vector<HeldKey> held_keys_;
    std::string active_scope_ = kScopeProgram;
};

}  // namespace XER::VideoEditor::Ui2
