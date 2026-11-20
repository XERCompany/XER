// browser_panel_widget.h — right-panel tab hosting a real Chromium
// browser (src/cef CefHost) plus a minimal navigation chrome: back /
// forward / reload / URL field / status. When the CEF distribution is
// not installed the panel degrades to an explanatory placeholder —
// everything still works, the web area just never opens.
#pragma once

#include <memory>
#include <string>

#include "widgets/widget.h"
#include "cef/cef_host.h"

namespace XER::UI::Widgets {
class Button;
class TextInput;
}

namespace XER::VideoEditor {

class BrowserPanelWidget final : public XER::UI::Widgets::Widget {
public:
    // parent_hwnd: HWND of the hosting app window — CefHost parents the
    // Chromium render window under it.
    explicit BrowserPanelWidget(void* parent_hwnd);
    ~BrowserPanelWidget() override;

    void Render(XER::UI::Rendering::Renderer& renderer,
                const XER::UI::Theme::Tokens& tokens) override;
    void Layout(const XER::UI::Layout::Rect& bounds) override;
    using Widget::Layout;
    bool HandleEvent(const XER::UI::Widgets::Event& event) override;
    XER::UI::Layout::Size GetMinSize() const override { return {120, 80}; }
    XER::UI::Layout::Size GetPreferredSize() const override {
        return {380, 320};
    }
    XER::UI::Layout::Size GetMaxSize() const override {
        return {4096, 4096};
    }
    // Shadow (base is non-virtual): toggles the Chromium child HWND
    // alongside the widget flag — the app calls through the derived
    // pointer.
    void SetVisible(bool visible);

    // Public navigation entry — used by the extension host to open
    // xer-ext:// pages on user request.
    void OpenUrl(const std::string& url) { Navigate(url); }

private:
    void EnsureBrowser();
    void Navigate(const std::string& input);
    void SyncBrowserRect();
    // Physical-pixel rect for the Chromium child HWND (view_rect_ is
    // DIPs): clipped to the widget's visible rect and scaled by the
    // host window's DPI.
    struct PxRect { int x, y, w, h; };
    PxRect BrowserRectPx() const;

    // CEF-thread event sink: HostEvents callbacks hold a weak_ptr to
    // this block and drop events once it expires (widget teardown), so
    // a callback in flight during ~BrowserPanelWidget can never touch
    // freed widget state. Render() drains it under the sink mutex on
    // the UI thread — the same pattern PreviewEngine pending_* uses.
    struct CefEvents;
    std::shared_ptr<CefEvents> cef_events_;

    void* parent_hwnd_ = nullptr;
    std::shared_ptr<XER::UI::Widgets::Button> back_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> fwd_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> reload_btn_;
    std::shared_ptr<XER::UI::Widgets::TextInput> url_input_;
    std::unique_ptr<XER::Cef::CefHost> cef_;
    XER::UI::Layout::Rect view_rect_{};
    std::string status_;   // UI thread only
    bool cef_tried_ = false;
    bool cef_ok_ = false;
};

}  // namespace XER::VideoEditor
