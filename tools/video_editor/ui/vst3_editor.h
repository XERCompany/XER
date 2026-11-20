// Hosts a VST3 plugin's native editor view (IPlugView) in a real
// Win32 popup window — the "given a clip's fx node, show its editor"
// path (Phase 2). Exe-side only (ui/ is Windows-gated in CMake); all
// VST3 work goes through the repo's host interface
// (Vst3HostInterface::CreatePluginEditor / GetPluginEditorSize /
// AttachPluginEditor), no IPlugView plumbing is reimplemented here.
//
// Window model: the plugin view attaches into an owned popup
// (WS_POPUPWINDOW | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME, owner =
// the app's main HWND) sized from IPlugView::getSize when the plugin
// reports one and centered over the main window. Being a top-level
// window it receives focus/mouse/keyboard like any desktop app — no
// forwarding layer is needed. The X button delivers WM_CLOSE, which
// runs the same Hide() path as programmatic close.
//
// Lifecycle: one editing session at a time. When a session ends —
// user close, a new Show()/Attach() replacing it, or destruction —
// on_closed fires ONCE while the plugin instance is still alive so
// the caller can harvest CurrentParams() and persist them. The
// callback must not reenter Show/Attach/Hide (guarded no-op).
// UI thread only.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>

#include "timeline_model.h"   // ClipAudioFx

namespace XER::Audio::Plugin {
class Vst3HostInterface;
}
namespace XER::VideoEditor::Vst {
class Vst3PluginInstance;
}

namespace XER::VideoEditor::Ui {

// One plugin editor session: an owned popup window + the attached
// IPlugView. Owns the plugin instance when created via Show(fx, ...);
// borrows it when created via Attach(instance, ...)/AttachHost.
class Vst3PluginEditor {
public:
    // Defined in .cc — in-class defaulting would instantiate
    // ~unique_ptr<incomplete Vst3PluginInstance> in callers' TUs.
    Vst3PluginEditor();
    ~Vst3PluginEditor();   // Hide()
    Vst3PluginEditor(const Vst3PluginEditor&) = delete;
    Vst3PluginEditor& operator=(const Vst3PluginEditor&) = delete;

    // Fires once per session when it ends — inside Hide(), before the
    // plugin view is detached and the instance released, so
    // CurrentParams() still reads live values. Must not call
    // Show/Attach/Hide (reentrancy is a guarded no-op).
    void SetOnClosed(std::function<void()> fn) {
        on_closed_ = std::move(fn);
    }

    // Loads fx.plugin_path through the production VST3 host
    // (dual-mono instance kept alive for the editor session), applies
    // fx.params, creates the plugin's IPlugView and attaches it inside
    // a popup centered on `owner_hwnd` (sized from the view's
    // preferred size when reported). Fails cleanly — returns false +
    // error_out, leaves nothing shown — when the plugin cannot load
    // or exposes no editor view.
    bool Show(const ClipAudioFx& fx, void* owner_hwnd,
              std::string* error_out = nullptr,
              double sample_rate = 48000.0,
              std::uint32_t max_block_size = 1024);

    // Attaches an already-live instance's editor (the caller owns the
    // instance — it must outlive this editor's attached state).
    bool Attach(Vst::Vst3PluginInstance* instance, void* owner_hwnd,
                std::string* error_out = nullptr);

    // Lowest-level entry: attach a bare host's editor view. The host
    // must outlive the attached state.
    bool AttachHost(XER::Audio::Plugin::Vst3HostInterface* host,
                    void* owner_hwnd, std::string* error_out = nullptr);

    // Detaches the plugin view, destroys the popup, and releases the
    // owned instance (if any). Fires on_closed first when a session
    // was attached. Safe to call anytime.
    void Hide();

    bool IsShowing() const { return attached_; }
    std::string PluginName() const;

    // Normalized parameters the plugin declares, filtered to those
    // whose current value differs from their default — exactly what
    // ClipAudioFx::params should store after the session. Empty when
    // nothing is attached.
    std::map<std::uint32_t, float> CurrentParams() const;

    // Popup WndProc sinks — the HWND carries `this` in GWLP_USERDATA
    // and the class WndProc (file-local in the .cc) forwards here.
    // Not for general use.
    void OnPopupResized(int w, int h);
    void OnPopupClose();

private:

    std::unique_ptr<Vst::Vst3PluginInstance> owned_instance_;
    // Instance driving the editor — owned_instance_.get() or the
    // Attach() caller's (borrowed; null for bare AttachHost()).
    Vst::Vst3PluginInstance* instance_ = nullptr;
    // Host currently driving the editor (borrowed — owned by
    // the instance or the AttachHost() caller).
    XER::Audio::Plugin::Vst3HostInterface* host_ = nullptr;
    void* popup_hwnd_ = nullptr;   // owned popup HWND
    std::function<void()> on_closed_;
    bool attached_ = false;
    bool closing_ = false;   // reentrancy guard for Hide()
};

}  // namespace XER::VideoEditor::Ui
