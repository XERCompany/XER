// browser_panel_widget.cc — see the header for the contract. The CEF
// runtime is initialized lazily on first layout so a missing install
// costs nothing at startup. HostEvents arrive on CEF's UI thread — they
// stash strings under a mutex and Render() applies them on the app UI
// thread (the same pattern the preview engine's pending-playhead uses).
#include "browser_panel_widget.h"

#include <mutex>

#include "cef/cef_runtime.h"
#include "rendering/renderer.h"
#include "widgets/button.h"
#include "widgets/text_input.h"

namespace XER::VideoEditor {
namespace {

constexpr float kChromeH = 30.0f;
constexpr float kStatusH = 18.0f;
constexpr float kPad = 8.0f;

std::mutex g_event_mutex;
std::string g_pending_url;
std::string g_pending_title;

}  // namespace

BrowserPanelWidget::BrowserPanelWidget(void* parent_hwnd)
    : parent_hwnd_(parent_hwnd) {
    back_btn_ = std::make_shared<XER::UI::Widgets::Button>(
        "<", XER::UI::Widgets::ButtonStyle::Secondary);
    back_btn_->SetTooltip("Back");
    back_btn_->SetOnClick([this] {
        if (cef_) cef_->GoBack();
    });
    fwd_btn_ = std::make_shared<XER::UI::Widgets::Button>(
        ">", XER::UI::Widgets::ButtonStyle::Secondary);
    fwd_btn_->SetTooltip("Forward");
    fwd_btn_->SetOnClick([this] {
        if (cef_) cef_->GoForward();
    });
    reload_btn_ = std::make_shared<XER::UI::Widgets::Button>(
        "R", XER::UI::Widgets::ButtonStyle::Secondary);
    reload_btn_->SetTooltip("Reload");
    reload_btn_->SetOnClick([this] {
        if (cef_) cef_->Reload();
    });
    url_input_ = std::make_shared<XER::UI::Widgets::TextInput>();
    url_input_->SetPlaceholder("https://...");
    url_input_->SetOnSubmit([this] { Navigate(url_input_->GetValue()); });
    AddChild(back_btn_);
    AddChild(fwd_btn_);
    AddChild(reload_btn_);
    AddChild(url_input_);
}

BrowserPanelWidget::~BrowserPanelWidget() {
    if (cef_) {
        cef_->Close();
    }
}

void BrowserPanelWidget::EnsureBrowser() {
    if (cef_tried_) {
        return;
    }
    cef_tried_ = true;
    std::string error;
    if (!XER::Cef::Available()) {
        status_ = "Chromium runtime not compiled in";
        return;
    }
    if (!XER::Cef::Initialize({}, &error)) {
        status_ = "Chromium runtime unavailable: " + error;
        return;
    }
    cef_ = std::make_unique<XER::Cef::CefHost>();
    XER::Cef::HostEvents events;
    events.on_url = [](const std::string& url) {
        std::lock_guard<std::mutex> lk(g_event_mutex);
        g_pending_url = url;
    };
    events.on_title = [](const std::string& title) {
        std::lock_guard<std::mutex> lk(g_event_mutex);
        g_pending_title = title;
    };
    events.on_loading = [this](bool loading) {
        status_ = loading ? "Loading..." : "";
    };
    cef_ok_ = cef_->Create(static_cast<CefHostNativeHandle>(parent_hwnd_),
                           static_cast<int>(view_rect_.x),
                           static_cast<int>(view_rect_.y),
                           static_cast<int>(view_rect_.width),
                           static_cast<int>(view_rect_.height),
                           "about:blank", std::move(events));
    if (!cef_ok_) {
        status_ = "Failed to create browser window";
        cef_.reset();
    }
}

void BrowserPanelWidget::Navigate(const std::string& input) {
    if (input.empty() || !cef_) {
        return;
    }
    std::string url = input;
    // Bare words go to search; dotted strings get a scheme — the same
    // heuristic every embedded browser uses.
    const bool has_scheme = url.find("://") != std::string::npos;
    if (!has_scheme) {
        if (url.find(' ') == std::string::npos &&
            url.find('.') != std::string::npos) {
            url = "https://" + url;
        } else {
            url = "https://www.google.com/search?q=" + url;
        }
    }
    cef_->LoadUrl(url);
}

void BrowserPanelWidget::SyncBrowserRect() {
    if (cef_ && cef_ok_) {
        cef_->Resize(static_cast<int>(view_rect_.x),
                     static_cast<int>(view_rect_.y),
                     static_cast<int>(view_rect_.width),
                     static_cast<int>(view_rect_.height));
    }
}

void BrowserPanelWidget::Layout(const XER::UI::Layout::Rect& bounds) {
    SetBounds(bounds);
    const auto& tokens = CachedLayoutTokens();
    const float bx = bounds.x + kPad;
    float y = bounds.y + kPad;
    const float btn_w = 26.0f;
    back_btn_->Layout({bx, y, btn_w, kChromeH - 6.0f}, tokens);
    fwd_btn_->Layout({bx + btn_w + 4.0f, y, btn_w, kChromeH - 6.0f},
                     tokens);
    reload_btn_->Layout(
        {bx + 2.0f * (btn_w + 4.0f), y, btn_w, kChromeH - 6.0f}, tokens);
    const float input_x = bx + 3.0f * (btn_w + 4.0f) + 4.0f;
    url_input_->Layout(
        {input_x, y, bounds.x + bounds.width - kPad - input_x,
         kChromeH - 6.0f},
        tokens);
    y += kChromeH + 2.0f;
    view_rect_ = {bx, y, bounds.width - 2.0f * kPad,
                  bounds.y + bounds.height - kPad - kStatusH - y};
    EnsureBrowser();
    SyncBrowserRect();
}

bool BrowserPanelWidget::HandleEvent(
    const XER::UI::Widgets::Event& event) {
    // Clicks in the Chromium area focus the browser so it starts
    // receiving OS input; the child HWND owns real hit-testing.
    if (event.type == XER::UI::Widgets::Event::Type::MouseDown &&
        view_rect_.Contains(event.position) && cef_ && cef_ok_) {
        cef_->Focus();
        return true;
    }
    return false;
}

void BrowserPanelWidget::SetVisible(bool visible) {
    XER::UI::Widgets::Widget::SetVisible(visible);
    if (cef_) {
        cef_->Show(visible);
    }
}

void BrowserPanelWidget::Render(
    XER::UI::Rendering::Renderer& renderer,
    const XER::UI::Theme::Tokens& tokens) {
    // Apply CEF-thread events on the UI thread.
    {
        std::lock_guard<std::mutex> lk(g_event_mutex);
        if (!g_pending_url.empty()) {
            url_input_->SetValue(g_pending_url);
            g_pending_url.clear();
        }
        if (!g_pending_title.empty()) {
            g_pending_title.clear();
        }
    }
    const auto bounds = GetBounds();
    renderer.FillRect(bounds, tokens.surface, tokens.radius_large);
    renderer.StrokeRect(bounds, tokens.border, 1.0f, tokens.radius_large);
    // Browser region: dark well under the chromium child (visible as a
    // flash before the page paints, and as the fallback panel).
    const auto vr = view_rect_;
    if (!cef_ok_) {
        renderer.FillRect(vr, tokens.input, tokens.radius_small);
        renderer.DrawText(
            status_.empty() ? "Web panel" : status_,
            {vr.x + 8.0f, vr.y + 8.0f, vr.width - 16.0f, 20.0f},
            XER::UI::Rendering::TextStyle{tokens.muted,
                                          tokens.font_small, 400});
        renderer.DrawText(
            "Install CEF via InstallCef.ps1 to enable HTML views",
            {vr.x + 8.0f, vr.y + 30.0f, vr.width - 16.0f, 20.0f},
            XER::UI::Rendering::TextStyle{tokens.muted,
                                          tokens.font_small, 400});
    }
    // Status strip.
    if (!status_.empty()) {
        renderer.DrawText(
            status_,
            {bounds.x + kPad,
             bounds.y + bounds.height - kStatusH - 2.0f,
             bounds.width - 2.0f * kPad, kStatusH},
            XER::UI::Rendering::TextStyle{tokens.muted,
                                          tokens.font_small, 400});
    }
}

}  // namespace XER::VideoEditor
