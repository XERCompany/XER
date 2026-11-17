// Hosts a VST3 plugin's native editor view (IPlugView) inside the
// video editor's Win32 UI — the "given a clip's fx node, show its
// editor" path (Phase 2). Exe-side only (ui/ is Windows-gated in
// CMake); all VST3 work goes through the repo's host interface
// (Vst3HostInterface::CreatePluginEditor / AttachPluginEditor), no
// IPlugView plumbing is reimplemented here.
//
// Lifetime model: the plugin view is attached into a dedicated child
// HWND this object creates inside the caller's parent window — so
// Hide()/Resize()/destruction fully control the native view without
// touching the parent's other children. UI thread only.
#pragma once

#include <cstdint>
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

// Pixel rect in the parent HWND's client area.
struct Vst3EditorRect {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
    int Width() const { return right - left; }
    int Height() const { return bottom - top; }
};

// One plugin editor session: a child HWND + the attached IPlugView.
// Owns the plugin instance when created via Show(fx, ...); borrows it
// when created via Attach(instance, ...).
class Vst3PluginEditor {
public:
    // Defined in .cc — in-class defaulting would instantiate
    // ~unique_ptr<incomplete Vst3PluginInstance> in callers' TUs.
    Vst3PluginEditor();
    ~Vst3PluginEditor();   // Hide()
    Vst3PluginEditor(const Vst3PluginEditor&) = delete;
    Vst3PluginEditor& operator=(const Vst3PluginEditor&) = delete;

    // Loads fx.plugin_path through the production VST3 host
    // (dual-mono instance kept alive for the editor session), applies
    // fx.params, creates the plugin's IPlugView and attaches it inside
    // a child HWND covering `rect` (client pixels of `parent_hwnd`).
    // Fails cleanly — returns false + error_out, leaves nothing shown —
    // when the plugin cannot load or exposes no editor view.
    bool Show(const ClipAudioFx& fx, void* parent_hwnd,
              const Vst3EditorRect& rect,
              std::string* error_out = nullptr,
              double sample_rate = 48000.0,
              std::uint32_t max_block_size = 1024);

    // Attaches an already-live instance's editor (the caller owns the
    // instance — it must outlive this editor's attached state).
    bool Attach(Vst::Vst3PluginInstance* instance, void* parent_hwnd,
                const Vst3EditorRect& rect,
                std::string* error_out = nullptr);

    // Lowest-level entry: attach a bare host's editor view. The host
    // must outlive the attached state.
    bool AttachHost(XER::Audio::Plugin::Vst3HostInterface* host,
                    void* parent_hwnd, const Vst3EditorRect& rect,
                    std::string* error_out = nullptr);

    // Repositions the child HWND and the plugin view frame.
    void Resize(const Vst3EditorRect& rect);

    // Detaches the plugin view, destroys the child HWND, and releases
    // the owned instance (if any). Safe to call anytime.
    void Hide();

    bool IsShowing() const { return attached_; }
    std::string PluginName() const;

private:
    std::unique_ptr<Vst::Vst3PluginInstance> owned_instance_;
    // Instance driving the editor — owned_instance_.get() or the
    // Attach() caller's (borrowed; null for bare AttachHost()).
    Vst::Vst3PluginInstance* instance_ = nullptr;
    // Host currently driving the editor (borrowed — owned by
    // the instance or the AttachHost() caller).
    XER::Audio::Plugin::Vst3HostInterface* host_ = nullptr;
    void* child_hwnd_ = nullptr;   // HWND of the container child window
    Vst3EditorRect rect_;
    bool attached_ = false;
};

}  // namespace XER::VideoEditor::Ui
