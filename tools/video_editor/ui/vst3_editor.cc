// vst3_editor.cc — see vst3_editor.h. Win32: the plugin's IPlugView
// parents into a dedicated WS_CHILD container window we create inside
// the caller's parent HWND, so show/hide/resize map onto ordinary
// window operations plus the host's AttachPluginEditor /
// SetPluginEditorFrame / RemovePluginEditor calls.
#include "vst3_editor.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <utility>

#include "audio/plugin_wrappers/vst3_host_interface.h"
#include "vst/vst3_factory.h"

namespace XER::VideoEditor::Ui {
namespace {

const wchar_t kHostClassName[] = L"XerVst3EditorHost";

// One-time registration of the container child-window class.
void EnsureHostClass() {
    static const bool registered = [] {
        WNDCLASSEXW wc {};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));   // IDC_ARROW
        wc.hbrBackground =
            reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
        wc.lpszClassName = kHostClassName;
        return RegisterClassExW(&wc) != 0;
    }();
    (void)registered;
}

bool Fail(std::string* error_out, const std::string& message) {
    if (error_out != nullptr) {
        *error_out = message;
    }
    return false;
}

}  // namespace

Vst3PluginEditor::Vst3PluginEditor() = default;
Vst3PluginEditor::~Vst3PluginEditor() { Hide(); }

bool Vst3PluginEditor::Show(const ClipAudioFx& fx, void* parent_hwnd,
                            const Vst3EditorRect& rect,
                            std::string* error_out, double sample_rate,
                            std::uint32_t max_block_size) {
    Hide();
    std::string error;
    auto instance = Vst::Vst3PluginInstance::Create(
        fx.plugin_path, sample_rate, max_block_size, &error);
    if (instance == nullptr) {
        return Fail(error_out,
                    error.empty() ? "plugin load failed" : error);
    }
    for (const auto& [id, value] : fx.params) {
        instance->SetParameter(id, value);
    }
    owned_instance_ = std::move(instance);
    instance_ = owned_instance_.get();
    if (!AttachHost(owned_instance_->PrimaryHost(), parent_hwnd, rect,
                    error_out)) {
        Hide();   // releases the owned instance too
        return false;
    }
    return true;
}

bool Vst3PluginEditor::Attach(Vst::Vst3PluginInstance* instance,
                              void* parent_hwnd,
                              const Vst3EditorRect& rect,
                              std::string* error_out) {
    Hide();
    if (instance == nullptr) {
        return Fail(error_out, "null plugin instance");
    }
    instance_ = instance;
    if (!AttachHost(instance->PrimaryHost(), parent_hwnd, rect,
                    error_out)) {
        Hide();
        return false;
    }
    return true;
}

bool Vst3PluginEditor::AttachHost(
    XER::Audio::Plugin::Vst3HostInterface* host, void* parent_hwnd,
    const Vst3EditorRect& rect, std::string* error_out) {
    if (host == nullptr) {
        return Fail(error_out, "no plugin host");
    }
    if (parent_hwnd == nullptr) {
        return Fail(error_out, "parent window required");
    }
    if (!host->HasPluginEditor()) {
        return Fail(error_out, "plugin has no editor view");
    }
    EnsureHostClass();
    const int w = std::max(1, rect.Width());
    const int h = std::max(1, rect.Height());
    HWND child = CreateWindowExW(
        0, kHostClassName, L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
        rect.left, rect.top, w, h,
        static_cast<HWND>(parent_hwnd), nullptr,
        GetModuleHandleW(nullptr), nullptr);
    if (child == nullptr) {
        return Fail(error_out, "editor container window create failed");
    }
    std::string error;
    if (!host->CreatePluginEditor(&error)) {
        DestroyWindow(child);
        return Fail(error_out, error.empty()
                                   ? "plugin editor create failed"
                                   : error);
    }
    if (!host->AttachPluginEditor(child, 0, 0, w, h, &error)) {
        host->RemovePluginEditor();
        DestroyWindow(child);
        return Fail(error_out, error.empty()
                                   ? "plugin editor attach failed"
                                   : error);
    }
    host_ = host;
    child_hwnd_ = child;
    rect_ = rect;
    attached_ = true;
    return true;
}

void Vst3PluginEditor::Resize(const Vst3EditorRect& rect) {
    rect_ = rect;
    const int w = std::max(1, rect.Width());
    const int h = std::max(1, rect.Height());
    if (child_hwnd_ != nullptr) {
        MoveWindow(static_cast<HWND>(child_hwnd_), rect.left, rect.top,
                   w, h, TRUE);
    }
    if (attached_ && host_ != nullptr) {
        // Plugin-view rect is in the CONTAINER child's client area.
        host_->SetPluginEditorFrame(0, 0, w, h);
    }
}

void Vst3PluginEditor::Hide() {
    if (attached_ && host_ != nullptr) {
        host_->RemovePluginEditor();
    }
    attached_ = false;
    host_ = nullptr;
    instance_ = nullptr;
    if (child_hwnd_ != nullptr) {
        DestroyWindow(static_cast<HWND>(child_hwnd_));
        child_hwnd_ = nullptr;
    }
    owned_instance_.reset();
}

std::string Vst3PluginEditor::PluginName() const {
    return instance_ != nullptr ? instance_->Name() : std::string();
}

}  // namespace XER::VideoEditor::Ui
