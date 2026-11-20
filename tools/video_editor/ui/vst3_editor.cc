// vst3_editor.cc — see vst3_editor.h. The plugin's IPlugView parents
// into an owned popup window so the native view gets ordinary desktop
// input/focus and a caption with a working X button. Placement math
// (preferred-size pick, centering, work-area clamp) lives in
// vst/fx_editor_utils.h where the unit tests can reach it; the Win32
// glue (class registration, popup WndProc, DPI-aware outer sizing)
// stays here.
#include "vst3_editor.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <utility>
#include <vector>

#include "audio/plugin_wrappers/vst3_host_interface.h"
#include "helper/string_utils.h"
#include "vst/fx_editor_utils.h"
#include "vst/vst3_factory.h"

namespace XER::VideoEditor::Ui {
namespace {

const wchar_t kPopupClassName[] = L"XerVst3EditorPopup";

Vst3PluginEditor* EditorOf(HWND hwnd) {
    return reinterpret_cast<Vst3PluginEditor*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

LRESULT CALLBACK PopupProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_NCCREATE: {
            // lpCreateParams carries the `this` passed to
            // CreateWindowExW — stash it before any other message can
            // need it.
            const auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(
                                  cs->lpCreateParams));
            return DefWindowProcW(hwnd, msg, wp, lp);
        }
        case WM_SIZE: {
            if (Vst3PluginEditor* editor = EditorOf(hwnd)) {
                editor->OnPopupResized(static_cast<int>(LOWORD(lp)),
                                       static_cast<int>(HIWORD(lp)));
            }
            return 0;
        }
        case WM_CLOSE: {
            if (Vst3PluginEditor* editor = EditorOf(hwnd)) {
                editor->OnPopupClose();   // Hide() — commits, detaches
            }
            return 0;
        }
        case WM_DESTROY:
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            return DefWindowProcW(hwnd, msg, wp, lp);
        default:
            return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

// One-time registration of the popup window class.
void EnsurePopupClass() {
    static const bool registered = [] {
        WNDCLASSEXW wc {};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
        wc.lpfnWndProc = PopupProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));   // IDC_ARROW
        wc.hbrBackground =
            reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
        wc.lpszClassName = kPopupClassName;
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

bool Vst3PluginEditor::Show(const ClipAudioFx& fx, void* owner_hwnd,
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
    if (!AttachHost(owned_instance_->PrimaryHost(), owner_hwnd,
                    error_out)) {
        Hide();   // releases the owned instance too
        return false;
    }
    return true;
}

bool Vst3PluginEditor::Attach(Vst::Vst3PluginInstance* instance,
                              void* owner_hwnd,
                              std::string* error_out) {
    Hide();
    if (instance == nullptr) {
        return Fail(error_out, "null plugin instance");
    }
    instance_ = instance;
    if (!AttachHost(instance->PrimaryHost(), owner_hwnd, error_out)) {
        Hide();
        return false;
    }
    return true;
}

bool Vst3PluginEditor::AttachHost(
    XER::Audio::Plugin::Vst3HostInterface* host, void* owner_hwnd,
    std::string* error_out) {
    if (host == nullptr) {
        return Fail(error_out, "no plugin host");
    }
    if (owner_hwnd == nullptr) {
        return Fail(error_out, "owner window required");
    }
    if (!host->HasPluginEditor()) {
        return Fail(error_out, "plugin has no editor view");
    }
    std::string error;
    // Create the view first — its preferred size decides the popup's
    // client size (fallback when the plugin reports nothing sane).
    if (!host->CreatePluginEditor(&error)) {
        return Fail(error_out, error.empty()
                                   ? "plugin editor create failed"
                                   : error);
    }
    int preferred_w = 0, preferred_h = 0;
    host->GetPluginEditorSize(&preferred_w, &preferred_h);
    const Vst::EditorClientSize client =
        Vst::PickEditorClientSize(preferred_w, preferred_h, 720, 480);

    const HWND owner = static_cast<HWND>(owner_hwnd);
    RECT parent_rect {};
    GetWindowRect(owner, &parent_rect);
    RECT work {};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const Vst::EditorPopupPlacement place = Vst::CenterPopupOnParent(
        parent_rect.left, parent_rect.top, parent_rect.right,
        parent_rect.bottom, client.width, client.height, work.left,
        work.top, work.right, work.bottom);

    EnsurePopupClass();
    const DWORD style =
        WS_POPUPWINDOW | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME;
    const DWORD ex_style = WS_EX_TOOLWINDOW;
    // Client size -> outer window size at the owner's DPI (the popup
    // inherits it as an owned window).
    RECT outer {0, 0, client.width, client.height};
    const UINT dpi = GetDpiForWindow(owner);
    if (!AdjustWindowRectExForDpi(&outer, style, FALSE, ex_style,
                                  dpi != 0 ? dpi : USER_DEFAULT_SCREEN_DPI)) {
        AdjustWindowRectEx(&outer, style, FALSE, ex_style);
    }
    std::string title = host->GetPluginName();
    if (title.empty()) {
        title = "VST3 editor";
    }
    const std::wstring wtitle =
        XER::Helper::String::Utf8ToWide(title + " — plugin editor");
    HWND popup = CreateWindowExW(
        ex_style, kPopupClassName, wtitle.c_str(), style,
        place.left, place.top, outer.right - outer.left,
        outer.bottom - outer.top, owner, nullptr,
        GetModuleHandleW(nullptr), this);
    if (popup == nullptr) {
        host->RemovePluginEditor();
        return Fail(error_out, "editor popup window create failed");
    }
    // The view fills the popup's client area.
    if (!host->AttachPluginEditor(popup, 0, 0, client.width,
                                  client.height, &error)) {
        host->RemovePluginEditor();
        DestroyWindow(popup);
        return Fail(error_out, error.empty()
                                   ? "plugin editor attach failed"
                                   : error);
    }
    host_ = host;
    popup_hwnd_ = popup;
    attached_ = true;
    ShowWindow(popup, SW_SHOWNORMAL);
    UpdateWindow(popup);
    return true;
}

void Vst3PluginEditor::OnPopupResized(int w, int h) {
    if (attached_ && host_ != nullptr) {
        // User dragged the frame — the view tracks the new client
        // rect (plugins that can't resize just ignore onSize).
        host_->SetPluginEditorFrame(0, 0, w, h);
    }
}

void Vst3PluginEditor::OnPopupClose() { Hide(); }

void Vst3PluginEditor::Hide() {
    if (closing_) {
        return;   // reentrant call from inside on_closed_
    }
    closing_ = true;
    // Session ends: give the listener one last look at the live
    // plugin (CurrentParams) before the view detaches.
    if (attached_ && on_closed_) {
        on_closed_();
    }
    if (attached_ && host_ != nullptr) {
        host_->RemovePluginEditor();
    }
    attached_ = false;
    host_ = nullptr;
    instance_ = nullptr;
    if (popup_hwnd_ != nullptr) {
        DestroyWindow(static_cast<HWND>(popup_hwnd_));
        popup_hwnd_ = nullptr;
    }
    owned_instance_.reset();
    closing_ = false;
}

std::string Vst3PluginEditor::PluginName() const {
    return instance_ != nullptr ? instance_->Name() : std::string();
}

std::map<std::uint32_t, float> Vst3PluginEditor::CurrentParams() const {
    if (host_ == nullptr) {
        return {};
    }
    std::vector<Vst::EditorParamSnapshot> snapshots;
    for (const auto& declared : host_->GetParameters()) {
        float value = 0.0f;
        if (host_->GetParameter(declared.id, &value)) {
            snapshots.push_back(
                {declared.id, value, declared.default_value});
        }
    }
    return Vst::PersistableParamEdits(snapshots);
}

}  // namespace XER::VideoEditor::Ui
