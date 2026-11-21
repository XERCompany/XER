// browser_panel_widget.cc — see the header for the contract. The CEF
// runtime is initialized lazily on first layout so a missing install
// costs nothing at startup. HostEvents arrive on CEF's UI thread — they
// stash strings under a mutex and Render() applies them on the app UI
// thread (the same pattern the preview engine's pending-playhead uses).
#include "browser_panel_widget.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>   // GetDpiForWindow
#endif

#include <mutex>

#include "cef/cef_runtime.h"
#include "rendering/renderer.h"
#include "widgets/button.h"
#include "widgets/button_group.h"
#include "widgets/label.h"
#include "widgets/text_input.h"

namespace XER::VideoEditor {
namespace {

constexpr float kChromeH = 30.0f;
constexpr float kStatusH = 18.0f;
constexpr float kPad = 8.0f;

}  // namespace

// Per-instance event mailbox — CEF UI-thread callbacks write here
// under the mutex; Render() drains it on the app UI thread. Scoped to
// the widget (not file-static): a recreated panel never sees a dead
// panel's pending events, and a weak_ptr-held sink means callbacks
// outliving the widget resolve to nothing instead of a dangling this.
struct BrowserPanelWidget::CefEvents {
    std::mutex mutex;
    std::string url;
    std::string title;
    bool loading = false;
    bool has_loading = false;
};

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
    status_label_ =
        std::make_shared<XER::UI::Widgets::Label>("", /*muted=*/true);
    // Joined nav strip: the three buttons render edge-to-edge inside
    // one bordered surface (ButtonGroup), matching the grouped
    // transport rows elsewhere in the chrome.
    nav_group_ = std::make_shared<XER::UI::Widgets::ButtonGroup>(
        XER::UI::Widgets::ButtonGroup::Orientation::Horizontal);
    nav_group_->AddChild(back_btn_);
    nav_group_->AddChild(fwd_btn_);
    nav_group_->AddChild(reload_btn_);
    AddChild(nav_group_);
    AddChild(url_input_);
    AddChild(status_label_);
}

BrowserPanelWidget::~BrowserPanelWidget() {
    // Expire the event sink first: any HostEvents callback still in
    // flight on the CEF thread resolves its weak_ptr to nothing after
    // this — it can never write into a half-destroyed widget.
    cef_events_.reset();
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
    // disable_gpu: the panel hosts extension pages — no WebGL/game
    // content — and the GPU subprocess skips hardware probing entirely
    // (a software-composited Chromium child is cheaper and driver-
    // independent).
    XER::Cef::RuntimeOptions cef_options;
    cef_options.disable_gpu = true;
    if (!XER::Cef::Initialize(cef_options, &error)) {
        status_ = "Chromium runtime unavailable: " + error;
        return;
    }
    cef_ = std::make_unique<XER::Cef::CefHost>();
    // DPI: widget bounds are DIPs, the browser child HWND wants client
    // pixels — the same conversion SyncBrowserRect applies.
    const auto rect = BrowserRectPx();
    // Callbacks capture a weak_ptr to the sink — never `this`: the CEF
    // thread may invoke them after the widget is gone.
    cef_events_ = std::make_shared<CefEvents>();
    const std::weak_ptr<CefEvents> sink = cef_events_;
    XER::Cef::HostEvents events;
    events.on_url = [sink](const std::string& url) {
        if (const auto s = sink.lock()) {
            std::lock_guard<std::mutex> lk(s->mutex);
            s->url = url;
        }
    };
    events.on_title = [sink](const std::string& title) {
        if (const auto s = sink.lock()) {
            std::lock_guard<std::mutex> lk(s->mutex);
            s->title = title;
        }
    };
    events.on_loading = [sink](bool loading) {
        if (const auto s = sink.lock()) {
            std::lock_guard<std::mutex> lk(s->mutex);
            s->loading = loading;
            s->has_loading = true;
        }
    };
    cef_ok_ = cef_->Create(static_cast<CefHostNativeHandle>(parent_hwnd_),
                           rect.x, rect.y, rect.w, rect.h,
                           "about:blank", std::move(events));
    if (!cef_ok_) {
        status_ = "Failed to create browser window";
        cef_.reset();
    }
    // A navigate that arrived before Create (extension UI opened
    // before this panel's first layout) replays now that the browser
    // exists — Navigate() re-runs its scheme heuristics on it.
    if (cef_ && !pending_url_.empty()) {
        const std::string url = std::move(pending_url_);
        pending_url_.clear();
        Navigate(url);
    }
}

void BrowserPanelWidget::Navigate(const std::string& input) {
    if (input.empty()) {
        return;
    }
    if (!cef_) {
        // Browser not created yet — stash it; EnsureBrowser loads it
        // right after Create (extension xer-ext:// opens can race
        // the first layout).
        pending_url_ = input;
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

// view_rect_ in DIPs -> the browser child HWND's rect in parent client
// pixels. Two corrections on the way: clip to the widget's visible
// rect (a native HWND is not clipped by the renderer, so a partially
// covered panel would otherwise leak the browser over neighbors), and
// scale by the window DPI (the app is per-monitor DPI aware; layout
// runs in DIPs while SetWindowPos wants pixels — mirrors
// FxRackWidget::SyncEditorFrame in demo/vst3_cef.cc).
BrowserPanelWidget::PxRect BrowserPanelWidget::BrowserRectPx() const {
    // GetVisibleRect() defaults to bounds_ when unset — intersecting
    // is always safe (view_rect_ lies inside bounds anyway).
    XER::UI::Layout::Rect clip = view_rect_.Intersect(GetVisibleRect());
    const bool has = clip.width > 1.0f && clip.height > 1.0f;
    if (!has) {
        // Nowhere to show: park the child on a 1x1 corner instead of
        // destroying it — it comes right back on the next layout.
        clip = {view_rect_.x, view_rect_.y, 1.0f, 1.0f};
    }
    float scale = 1.0f;
    if (parent_hwnd_ != nullptr) {
        const UINT dpi =
            GetDpiForWindow(static_cast<HWND>(parent_hwnd_));
        scale = dpi != 0 ? static_cast<float>(dpi) / 96.0f : 1.0f;
    }
    return {static_cast<int>(clip.x * scale),
            static_cast<int>(clip.y * scale),
            static_cast<int>(clip.width * scale),
            static_cast<int>(clip.height * scale)};
}

void BrowserPanelWidget::SyncBrowserRect() {
    if (cef_ && cef_ok_) {
        const auto rect = BrowserRectPx();
        cef_->Resize(rect.x, rect.y, rect.w, rect.h);
    }
}

void BrowserPanelWidget::Layout(const XER::UI::Layout::Rect& bounds) {
    SetBounds(bounds);
    const auto& tokens = CachedLayoutTokens();
    const float bx = bounds.x + kPad;
    float y = bounds.y + kPad;
    const float btn_w = 26.0f;
    nav_group_->Layout({bx, y, btn_w * 3.0f, kChromeH - 6.0f}, tokens);
    const float input_x = bx + btn_w * 3.0f + 8.0f;
    url_input_->Layout(
        {input_x, y, bounds.x + bounds.width - kPad - input_x,
         kChromeH - 6.0f},
        tokens);
    y += kChromeH + 2.0f;
    view_rect_ = {bx, y, bounds.width - 2.0f * kPad,
                  bounds.y + bounds.height - kPad - kStatusH - y};
    status_label_->Layout(
        {bounds.x + kPad, bounds.y + bounds.height - kStatusH - 2.0f,
         bounds.width - 2.0f * kPad, kStatusH},
        tokens);
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
    if (cef_events_) {
        std::lock_guard<std::mutex> lk(cef_events_->mutex);
        if (!cef_events_->url.empty()) {
            url_input_->SetValue(cef_events_->url);
            cef_events_->url.clear();
        }
        // The title is tracked but not drawn anywhere yet — drain so
        // the slot doesn't hold a stale value.
        cef_events_->title.clear();
        if (cef_events_->has_loading) {
            status_ = cef_events_->loading ? "Loading..." : "";
            cef_events_->has_loading = false;
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
    // Status strip is a Label child — feed it the drained status_ and
    // hide it when there is nothing to say.
    status_label_->SetText(status_);
    status_label_->SetVisible(!status_.empty());
}

}  // namespace XER::VideoEditor
