// xbrowser.cc — XBrowser: a tabbed Chromium browser built on the XER UI
// framework (src/ui) and XER::Cef::CefHost (src/cef).
//
// Architecture mirrors demo/vst3_cef.cc: one root widget owns the layout;
// each tab owns a CefHost whose Chromium child HWND is parented to the
// window and sized to the content rect. Only the active tab's browser is
// visible — inactive CefHosts are hidden via Show(false) and keep their
// state. Tab hosts are created lazily on first activation so a restored
// (but never visited) tab costs nothing.
//
// CefHost events fire on CEF's UI thread; per-tab callbacks only push
// into a shared TabPending struct which Tick() drains on the UI thread —
// no widget is ever touched off-thread.
//
// Controls:
//   tab strip        — click to switch, × or middle-click to close,
//                      + to open a new tab
//   toolbar          — < > R navigation, address bar, Go, DevTools
//   F5 / Ctrl+N      — reload / new tab
//   closing the last tab closes the window.

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>   // CommandLineToArgvW

#include <algorithm>
#include <atomic>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>

#include "cef/cef_host.h"
#include "cef/cef_runtime.h"
#include "helper/string_utils.h"
#include "ui/core/application.h"
#include "ui/core/window.h"
#include "ui/widgets/widget.h"
#include "ui/widgets/button.h"
#include "ui/widgets/label.h"
#include "ui/widgets/text_input.h"
#include "ui/rendering/renderer.h"

namespace XER::Demo {
namespace {

// New-tab landing page: branded tiles to common sites.
const char kHomeHtml[] = R"HTML(
<!doctype html><html><head><meta charset="utf-8"><title>New Tab</title>
<style>
body{background:#17181c;color:#d7d7de;font-family:Segoe UI,sans-serif;
     display:flex;align-items:center;justify-content:center;height:100vh;margin:0}
div{text-align:center}
h1{color:#7aa2f7;font-weight:600;font-size:42px;margin:0 0 28px}
.grid{display:grid;grid-template-columns:repeat(4,120px);gap:12px;
      justify-content:center}
a{display:block;padding:22px 0;background:#26272e;border-radius:10px;
  color:#d7d7de;text-decoration:none;font-size:14px;transition:.15s}
a:hover{background:#33343d;color:#7aa2f7}
</style></head><body><div>
<h1>XBrowser</h1>
<div class="grid">
<a href="https://www.google.com">Google</a>
<a href="https://github.com">GitHub</a>
<a href="https://www.youtube.com">YouTube</a>
<a href="https://en.wikipedia.org">Wikipedia</a>
</div></div></body></html>
)HTML";

// Per-tab handoff from CEF's UI thread to the app UI thread.
struct TabPending {
    std::mutex mutex;
    std::optional<std::string> url;
    std::optional<std::string> title;
    std::optional<bool> loading;
    bool closed = false;
};

// One browser tab: a lazily-created CefHost plus its pending events.
struct Tab {
    std::unique_ptr<Cef::CefHost> host;
    std::shared_ptr<TabPending> pending = std::make_shared<TabPending>();
    std::string title = "New Tab";
    std::string url;
    bool loading = false;
    bool dead = false;          // browser closed/crashed; recreate on activate
    bool create_tried = false;  // avoids a Create() retry loop on failure
};

// ---- URL helpers ---------------------------------------------------

std::string Trim(std::string s) {
    const auto not_space = [](char c) {
        return c != ' ' && c != '\t' && c != '\r' && c != '\n';
    };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
    s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
    return s;
}

std::string UrlEncode(const std::string& s) {
    std::string out;
    out.reserve(s.size() * 2);
    for (const unsigned char c : s) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' ||
            c == '.' || c == '~') {
            out.push_back(static_cast<char>(c));
        } else {
            char buf[4];
            std::snprintf(buf, sizeof(buf), "%%%02X", c);
            out += buf;
        }
    }
    return out;
}

// Address-bar semantics: full URLs pass through; bare domains get
// https://; anything else becomes a web search.
std::string NormalizeAddress(const std::string& raw) {
    const std::string in = Trim(raw);
    if (in.empty()) {
        return {};
    }
    const bool has_scheme = in.find("://") != std::string::npos ||
        in.rfind("about:", 0) == 0 || in.rfind("file:", 0) == 0 ||
        in.rfind("data:", 0) == 0;
    if (has_scheme) {
        return in;
    }
    const bool looks_like_host =
        in.find(' ') == std::string::npos &&
        (in.find('.') != std::string::npos ||
         in.rfind("localhost", 0) == 0);
    if (looks_like_host) {
        return "https://" + in;
    }
    return "https://www.google.com/search?q=" + UrlEncode(in);
}

// ---- Tab strip -----------------------------------------------------

// Custom tab bar: per-tab rect with truncated title, a × close button,
// and a trailing "+" new-tab button. Pure UI — reports through
// callbacks; tab content lives with the owner.
class TabStrip final : public UI::Widgets::Widget {
public:
    void SetTabs(std::vector<std::string> titles) {
        titles_ = std::move(titles);
        Invalidate();
    }
    void SetActive(int index) { active_ = index; Invalidate(); }
    void SetOnSelect(std::function<void(int)> cb) {
        on_select_ = std::move(cb);
    }
    void SetOnClose(std::function<void(int)> cb) {
        on_close_ = std::move(cb);
    }
    void SetOnNew(std::function<void()> cb) { on_new_ = std::move(cb); }

    UI::Layout::Size GetMinSize() const override { return {120.0f, 32.0f}; }
    UI::Layout::Size GetPreferredSize() const override {
        return {600.0f, 32.0f};
    }
    UI::Layout::Size GetMaxSize() const override {
        return {100000.0f, 32.0f};
    }

    void Layout(const UI::Layout::Rect& bounds) override {
        SetBounds(bounds);
        const float max_w = bounds.width - kPlusW;
        const int count = static_cast<int>(titles_.size());
        float w = count > 0 ? max_w / static_cast<float>(count) : 0.0f;
        w = std::min(kTabMaxW, std::max(kTabMinW, w));
        tab_rects_.clear();
        float x = bounds.x;
        for (int i = 0; i < count; ++i) {
            tab_rects_.push_back(
                {x, bounds.y, w, bounds.height});
            x += w + kGap;
        }
        plus_rect_ = {x, bounds.y + 2.0f, kPlusW, bounds.height - 4.0f};
    }
    using Widget::Layout;

    void Render(UI::Rendering::Renderer& renderer,
                const UI::Theme::Tokens& tokens) override {
        const UI::Rendering::TextStyle text_style{tokens.foreground,
                                                  12.0f, 400};
        for (size_t i = 0; i < tab_rects_.size(); ++i) {
            const auto& r = tab_rects_[i];
            const bool active = static_cast<int>(i) == active_;
            const bool hovered = static_cast<int>(i) == hovered_;
            if (active) {
                renderer.FillRect(r, tokens.surface, 8.0f);
                renderer.StrokeRect(r, tokens.border, 1.0f, 8.0f);
                // Accent underline for the active tab.
                renderer.FillRect({r.x + 8.0f, r.Bottom() - 2.0f,
                                   r.width - 16.0f, 2.0f},
                                  tokens.accent, 0.0f);
            } else if (hovered) {
                renderer.FillRect(r, tokens.hover, 8.0f);
            }

            const bool show_close =
                active || hovered || close_hovered_ == static_cast<int>(i);
            const UI::Layout::Rect title_rect{
                r.x + 10.0f, r.y,
                r.width - (show_close ? 34.0f : 18.0f), r.height};
            renderer.PushClip(title_rect);
            renderer.DrawText(
                titles_[i].empty() ? "New Tab" : titles_[i], title_rect,
                text_style);
            renderer.PopClip();

            if (show_close) {
                const UI::Layout::Rect cr = CloseRect(r);
                if (close_hovered_ == static_cast<int>(i)) {
                    renderer.FillRect(cr, tokens.hover, 4.0f);
                }
                // Hand-drawn × — no glyph dependency.
                const float cx = cr.x + 5.0f, cy = cr.y + 5.0f;
                const float cs = 8.0f;
                renderer.DrawLine({cx, cy}, {cx + cs, cy + cs},
                                  tokens.muted, 1.5f);
                renderer.DrawLine({cx + cs, cy}, {cx, cy + cs},
                                  tokens.muted, 1.5f);
            }
        }
        // "+" new-tab button.
        const bool plus_hover = hovered_ == -2;
        renderer.FillRect(plus_rect_,
                          plus_hover ? tokens.hover : tokens.surface,
                          6.0f);
        renderer.StrokeRect(plus_rect_, tokens.border, 1.0f, 6.0f);
        const float px = plus_rect_.x + plus_rect_.width * 0.5f;
        const float py = plus_rect_.y + plus_rect_.height * 0.5f;
        const float ps = 5.0f;
        renderer.DrawLine({px - ps, py}, {px + ps, py},
                          tokens.foreground, 1.5f);
        renderer.DrawLine({px, py - ps}, {px, py + ps},
                          tokens.foreground, 1.5f);
    }

    bool HandleEvent(const UI::Widgets::Event& event) override {
        using Type = UI::Widgets::Event::Type;
        if (event.type == Type::MouseMove) {
            const int hit = HitIndex(event.position);
            const int close_hit = HitClose(event.position);
            const int plus = HitPlus(event.position) ? -2 : -1;
            const int next = close_hit >= 0 ? close_hit
                                            : (plus == -2 ? -2 : hit);
            if (close_hit >= 0) {
                close_hovered_ = close_hit;
                hovered_ = close_hit;
            } else {
                close_hovered_ = -1;
                hovered_ = next;
            }
            Invalidate();
            return false;
        }
        if (event.type == Type::MouseDown) {
            const int close_hit = HitClose(event.position);
            if (close_hit >= 0 && on_close_) {
                on_close_(close_hit);
                return true;
            }
            if (HitPlus(event.position)) {
                if (on_new_) {
                    on_new_();
                }
                return true;
            }
            const int hit = HitIndex(event.position);
            if (hit >= 0) {
                if (event.mouse_button ==
                    UI::Widgets::MouseButton::Middle) {
                    if (on_close_) {
                        on_close_(hit);
                    }
                } else if (on_select_) {
                    on_select_(hit);
                }
                return true;
            }
        }
        return false;
    }

private:
    static constexpr float kTabMinW = 90.0f;
    static constexpr float kTabMaxW = 200.0f;
    static constexpr float kPlusW = 30.0f;
    static constexpr float kGap = 2.0f;

    UI::Layout::Rect CloseRect(const UI::Layout::Rect& tab) const {
        return {tab.Right() - 24.0f,
                tab.y + (tab.height - 18.0f) * 0.5f, 18.0f, 18.0f};
    }
    int HitIndex(const UI::Layout::Point& p) const {
        for (size_t i = 0; i < tab_rects_.size(); ++i) {
            if (tab_rects_[i].Contains(p)) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }
    int HitClose(const UI::Layout::Point& p) const {
        for (size_t i = 0; i < tab_rects_.size(); ++i) {
            const bool active = static_cast<int>(i) == active_;
            const bool hovered = static_cast<int>(i) == hovered_;
            if ((active || hovered) && CloseRect(tab_rects_[i]).Contains(p)) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }
    bool HitPlus(const UI::Layout::Point& p) const {
        return plus_rect_.Contains(p);
    }

    std::vector<std::string> titles_;
    std::vector<UI::Layout::Rect> tab_rects_;
    UI::Layout::Rect plus_rect_{};
    int active_ = -1;
    int hovered_ = -1;         // -2 = "+" button
    int close_hovered_ = -1;
    std::function<void(int)> on_select_;
    std::function<void(int)> on_close_;
    std::function<void()> on_new_;
};

// ---- Root browser widget -------------------------------------------

class XBrowserWidget final : public UI::Widgets::Widget {
public:
    XBrowserWidget(UI::Core::Window* window,
                   const std::vector<std::string>& urls,
                   std::string cef_error, bool cef_available)
        : window_(window)
        , parent_hwnd_(window != nullptr ? window->GetHWND() : nullptr)
        , cef_error_(std::move(cef_error))
        , cef_available_(cef_available) {
        strip_ = std::make_shared<TabStrip>();
        back_button_ = std::make_shared<UI::Widgets::Button>(
            "<", UI::Widgets::ButtonStyle::Outline);
        fwd_button_ = std::make_shared<UI::Widgets::Button>(
            ">", UI::Widgets::ButtonStyle::Outline);
        reload_button_ = std::make_shared<UI::Widgets::Button>(
            "R", UI::Widgets::ButtonStyle::Outline);
        url_input_ = std::make_shared<UI::Widgets::TextInput>();
        url_input_->SetPlaceholder("Search or type a URL");
        go_button_ = std::make_shared<UI::Widgets::Button>(
            "Go", UI::Widgets::ButtonStyle::Primary);
        devtools_button_ = std::make_shared<UI::Widgets::Button>(
            "DevTools", UI::Widgets::ButtonStyle::Outline);
        status_ = std::make_shared<UI::Widgets::Label>("", true);

        strip_->SetOnSelect([this](int i) { ActivateTab(i); });
        strip_->SetOnClose([this](int i) { CloseTab(i); });
        strip_->SetOnNew([this] { NewTab({}); });
        back_button_->SetOnClick(
            [this] { if (ActiveHost()) ActiveHost()->GoBack(); });
        fwd_button_->SetOnClick(
            [this] { if (ActiveHost()) ActiveHost()->GoForward(); });
        reload_button_->SetOnClick(
            [this] { if (ActiveHost()) ActiveHost()->Reload(); });
        devtools_button_->SetOnClick(
            [this] { if (ActiveHost()) ActiveHost()->ShowDevTools(); });
        go_button_->SetOnClick([this] { Navigate(); });
        url_input_->SetOnSubmit([this] { Navigate(); });

        AddChild(strip_);
        AddChild(back_button_);
        AddChild(fwd_button_);
        AddChild(reload_button_);
        AddChild(url_input_);
        AddChild(go_button_);
        AddChild(devtools_button_);
        AddChild(status_);

        // Initial tabs: one per URL, or a single home tab.
        if (urls.empty()) {
            NewTab({});
        } else {
            for (const std::string& u : urls) {
                NewTab(u);
            }
        }
        ActivateTab(0);
    }

    ~XBrowserWidget() override { Teardown(); }

    // Detaches and closes every tab's browser. Called before
    // Cef::Shutdown and again from the destructor — idempotent.
    void Teardown() {
        if (torn_down_) {
            return;
        }
        torn_down_ = true;
        for (auto& tab : tabs_) {
            if (tab->host) {
                tab->host->Close();
            }
        }
    }

    void Render(UI::Rendering::Renderer& renderer,
                const UI::Theme::Tokens& tokens) override {
        renderer.FillRect(GetBounds(), tokens.background, 0.0f);
        renderer.FillRect(content_rect_, tokens.surface,
                          tokens.radius_medium);
        renderer.StrokeRect(content_rect_, tokens.border, 1.0f,
                            tokens.radius_medium);
        // The active CefHost child HWND covers the pane; draw a
        // placeholder only when nothing lives there.
        const Tab* tab = ActiveTab();
        const bool empty = tab == nullptr || tab->host == nullptr;
        if (empty) {
            const char* text =
                !cef_available_
                    ? "CEF runtime unavailable"
                    : (cef_error_.empty() ? "browser not created"
                                          : cef_error_.c_str());
            const UI::Rendering::TextStyle style{tokens.muted, 14.0f, 400};
            const UI::Layout::Size size =
                renderer.MeasureText(text, style);
            renderer.DrawText(
                text,
                {content_rect_.x +
                     (content_rect_.width - size.width) * 0.5f,
                 content_rect_.y +
                     (content_rect_.height - size.height) * 0.5f,
                 size.width, size.height},
                style);
        }
    }

    void Layout(const UI::Layout::Rect& bounds) override {
        SetBounds(bounds);
        const auto& tokens = CachedLayoutTokens();
        const float pad = tokens.spacing_medium;
        const float gap = tokens.spacing_small;
        const float x0 = bounds.x + pad;
        const float w = bounds.width - 2.0f * pad;
        float y = bounds.y + pad;

        strip_->Layout({x0, y, w, 32.0f}, tokens);
        y += 32.0f + gap;

        const float row = 30.0f;
        back_button_->Layout({x0, y, 30.0f, row}, tokens);
        fwd_button_->Layout({x0 + 34.0f, y, 30.0f, row}, tokens);
        reload_button_->Layout({x0 + 68.0f, y, 30.0f, row}, tokens);
        devtools_button_->Layout(
            {x0 + w - 80.0f, y, 80.0f, row}, tokens);
        go_button_->Layout({x0 + w - 80.0f - 52.0f, y, 48.0f, row},
                           tokens);
        url_input_->Layout({x0 + 68.0f + 30.0f + gap, y,
                            w - 68.0f - 30.0f - 80.0f - 52.0f -
                                2.0f * gap,
                            row},
                           tokens);
        y += row + pad;

        const float status_h = 20.0f;
        content_rect_ = {x0, y, w,
                         bounds.Bottom() - pad - status_h - y};
        status_->Layout(
            {x0, bounds.Bottom() - pad - status_h + 3.0f, w, status_h},
            tokens);

        SyncActiveFrame();
    }
    using Widget::Layout;

    bool HandleEvent(const UI::Widgets::Event& event) override {
        if (event.type == UI::Widgets::Event::Type::KeyDown) {
            if (event.key == UI::Widgets::KeyCode::F5) {
                if (ActiveHost()) ActiveHost()->Reload();
                return true;
            }
            if (event.is_ctrl && event.key == UI::Widgets::KeyCode::N) {
                NewTab({});
                return true;
            }
            if (event.is_ctrl && event.key == UI::Widgets::KeyCode::O) {
                url_input_->SetFocused(true);
                return true;
            }
        }
        return false;
    }

    UI::Layout::Size GetMinSize() const override { return {480.0f, 320.0f}; }
    UI::Layout::Size GetPreferredSize() const override {
        return {1180.0f, 760.0f};
    }
    UI::Layout::Size GetMaxSize() const override {
        return {100000.0f, 100000.0f};
    }

    void Tick(double) override {
        PumpTabEvents();
        SyncActiveFrame();
    }

private:
    // ---- tabs -----------------------------------------------------

    Cef::CefHost* ActiveHost() {
        Tab* t = ActiveTab();
        return t != nullptr ? t->host.get() : nullptr;
    }
    Tab* ActiveTab() {
        if (active_ < 0 || active_ >= static_cast<int>(tabs_.size())) {
            return nullptr;
        }
        return tabs_[static_cast<size_t>(active_)].get();
    }

    void NewTab(const std::string& url) {
        auto tab = std::make_unique<Tab>();
        tab->url = NormalizeAddress(url);   // empty -> home page
        tabs_.push_back(std::move(tab));
        RefreshStrip();
        ActivateTab(static_cast<int>(tabs_.size()) - 1);
    }

    void CloseTab(int index) {
        if (index < 0 || index >= static_cast<int>(tabs_.size())) {
            return;
        }
        if (tabs_[static_cast<size_t>(index)]->host) {
            tabs_[static_cast<size_t>(index)]->host->Close();
        }
        tabs_.erase(tabs_.begin() + index);
        if (tabs_.empty()) {
            // Chrome semantics: closing the last tab closes the window.
            if (window_ != nullptr) {
                window_->Close();
            }
            return;
        }
        if (active_ >= index) {
            active_ = std::min(active_ > index ? active_ - 1 : active_,
                               static_cast<int>(tabs_.size()) - 1);
            if (active_ < 0) {
                active_ = 0;
            }
        }
        RefreshStrip();
        ActivateTab(active_);
    }

    void ActivateTab(int index) {
        if (index < 0 || index >= static_cast<int>(tabs_.size())) {
            return;
        }
        // Hide every other browser — only one child HWND is visible.
        for (size_t i = 0; i < tabs_.size(); ++i) {
            if (static_cast<int>(i) != index && tabs_[i]->host) {
                tabs_[i]->host->Show(false);
            }
        }
        active_ = index;
        strip_->SetActive(index);
        Tab* tab = ActiveTab();
        url_input_->SetValue(tab != nullptr ? tab->url : "");
        if (window_ != nullptr) {
            window_->SetTitle((tab != nullptr ? tab->title : "XBrowser") +
                              " — XBrowser");
        }
        SetStatus(tab != nullptr && tab->loading ? "Loading..."
                                                 : "Ready");
        SyncActiveFrame();
        Invalidate();
    }

    void RefreshStrip() {
        std::vector<std::string> titles;
        titles.reserve(tabs_.size());
        for (const auto& t : tabs_) {
            titles.push_back(t->title);
        }
        strip_->SetTabs(std::move(titles));
    }

    // Creates/positions the active tab's Chromium child HWND to the
    // content rect (DIP -> physical pixels, clamped to the viewport).
    void SyncActiveFrame() {
        Tab* tab = ActiveTab();
        if (torn_down_ || tab == nullptr || parent_hwnd_ == nullptr ||
            content_rect_.width <= 1.0f || content_rect_.height <= 1.0f) {
            return;
        }
        const float s = DpiScale();
        const UI::Layout::Rect rect =
            content_rect_.Intersect(GetVisibleRect());
        const int x = static_cast<int>(rect.x * s);
        const int y = static_cast<int>(rect.y * s);
        const int w = static_cast<int>(rect.width * s);
        const int h = static_cast<int>(rect.height * s);

        if (tab->dead) {
            tab->dead = false;
            tab->create_tried = false;
            tab->host.reset();      // drop the corpse; recreate below
        }
        if (tab->host == nullptr) {
            if (!cef_available_ || !tab->create_tried) {
                CreateHost(*tab, x, y, w, h);
            }
            return;
        }
        const UI::Layout::Rect scaled{static_cast<float>(x),
                                      static_cast<float>(y),
                                      static_cast<float>(x + w),
                                      static_cast<float>(y + h)};
        if (last_frame_.x != scaled.x || last_frame_.y != scaled.y ||
            last_frame_.width != scaled.width ||
            last_frame_.height != scaled.height) {
            tab->host->Resize(x, y, w, h);
            last_frame_ = scaled;
        }
        tab->host->Show(true);
    }

    void CreateHost(Tab& tab, int x, int y, int w, int h) {
        tab.create_tried = true;
        Cef::HostEvents events;
        auto pending = tab.pending;
        events.on_url = [pending](const std::string& url) {
            std::lock_guard<std::mutex> lk(pending->mutex);
            pending->url = url;
        };
        events.on_title = [pending](const std::string& title) {
            std::lock_guard<std::mutex> lk(pending->mutex);
            pending->title = title;
        };
        events.on_loading = [pending](bool loading) {
            std::lock_guard<std::mutex> lk(pending->mutex);
            pending->loading = loading;
        };
        events.on_closed = [pending] {
            std::lock_guard<std::mutex> lk(pending->mutex);
            pending->closed = true;
        };
        tab.host = std::make_unique<Cef::CefHost>();
        const std::string url =
            tab.url.empty() ? "about:blank" : tab.url;
        if (tab.host->Create(parent_hwnd_, x, y, w, h, url,
                             std::move(events))) {
            if (tab.url.empty()) {
                tab.host->LoadHtml(kHomeHtml);
            }
        } else {
            tab.host.reset();
            SetStatus("browser creation failed");
        }
    }

    void PumpTabEvents() {
        for (size_t i = 0; i < tabs_.size(); ++i) {
            Tab& tab = *tabs_[i];
            std::optional<std::string> url, title;
            std::optional<bool> loading;
            bool closed = false;
            {
                std::lock_guard<std::mutex> lk(tab.pending->mutex);
                url = std::exchange(tab.pending->url, std::nullopt);
                title = std::exchange(tab.pending->title, std::nullopt);
                loading =
                    std::exchange(tab.pending->loading, std::nullopt);
                closed = std::exchange(tab.pending->closed, false);
            }
            if (title) {
                tab.title = title->empty() ? "New Tab" : *title;
                RefreshStrip();
                if (static_cast<int>(i) == active_ &&
                    window_ != nullptr) {
                    window_->SetTitle(tab.title + " — XBrowser");
                }
            }
            if (url) {
                tab.url = *url;
                if (static_cast<int>(i) == active_) {
                    url_input_->SetValue(*url);
                }
            }
            if (loading) {
                tab.loading = *loading;
                if (static_cast<int>(i) == active_) {
                    SetStatus(*loading ? "Loading " + tab.url + " ..."
                                       : "Ready — " + tab.url);
                }
            }
            if (closed) {
                tab.dead = true;
                if (static_cast<int>(i) == active_) {
                    SetStatus("browser process ended — reactivating...");
                }
            }
        }
    }

    void Navigate() {
        Tab* tab = ActiveTab();
        if (tab == nullptr || tab->host == nullptr) {
            return;
        }
        const std::string url = NormalizeAddress(url_input_->GetValue());
        if (!url.empty()) {
            tab->host->LoadUrl(url);
        }
    }

    void SetStatus(const std::string& text) { status_->SetText(text); }

    float DpiScale() const {
        if (parent_hwnd_ == nullptr) {
            return 1.0f;
        }
        const UINT dpi =
            GetDpiForWindow(static_cast<HWND>(parent_hwnd_));
        return dpi != 0 ? static_cast<float>(dpi) / 96.0f : 1.0f;
    }

    UI::Core::Window* window_ = nullptr;
    void* parent_hwnd_ = nullptr;
    std::string cef_error_;
    bool cef_available_ = false;
    bool torn_down_ = false;

    std::shared_ptr<TabStrip> strip_;
    std::shared_ptr<UI::Widgets::Button> back_button_;
    std::shared_ptr<UI::Widgets::Button> fwd_button_;
    std::shared_ptr<UI::Widgets::Button> reload_button_;
    std::shared_ptr<UI::Widgets::TextInput> url_input_;
    std::shared_ptr<UI::Widgets::Button> go_button_;
    std::shared_ptr<UI::Widgets::Button> devtools_button_;
    std::shared_ptr<UI::Widgets::Label> status_;

    std::vector<std::unique_ptr<Tab>> tabs_;
    int active_ = -1;
    UI::Layout::Rect content_rect_{};
    UI::Layout::Rect last_frame_{};
};

} // namespace

int RunXBrowser(const std::vector<std::string>& urls) {
    auto& app = XER::UI::Core::Application::Instance();
    if (!app.Initialize()) {
        std::fprintf(stderr, "UI application init failed\n");
        return 1;
    }
    app.SetAppName("XBrowser");

    // CEF echoes ERROR+ to stderr even with a log file — LOGSEVERITY_
    // DISABLE (99) is the only level that keeps the console clean of
    // Chromium's registry/extension chatter; FATAL still reaches it.
    XER::Cef::RuntimeOptions cef_opts;
    cef_opts.log_severity = 99;   // LOGSEVERITY_DISABLE
    if (std::getenv("XER_CEF_DEBUG")) {
        cef_opts.log_severity = 1;   // LOGSEVERITY_VERBOSE
    }
    // Let pages autoplay media without a user gesture.
    cef_opts.command_line_switches = {
        "autoplay-policy=no-user-gesture-required",
    };
    wchar_t temp[MAX_PATH]{};
    if (GetTempPathW(MAX_PATH, temp) != 0) {
        const std::string temp_dir =
            XER::Helper::String::WideToUtf8(temp);
        cef_opts.log_file = temp_dir + "xbrowser_debug.log";
        // CEF locks the profile dir process-wide; without an explicit
        // cache dir every CEF app shares %LOCALAPPDATA%\CEF\User Data
        // and only the first instance initializes. A per-instance dir
        // lets XBrowser run beside the VST3 demo (and itself).
        cef_opts.cache_dir = temp_dir + "xbrowser_cef_" +
                             std::to_string(GetCurrentProcessId());
    }
    std::string cef_error;
    const bool cef_ok = XER::Cef::Initialize(cef_opts, &cef_error);
    if (!cef_ok) {
        std::fprintf(stderr, "CEF unavailable: %s\n", cef_error.c_str());
    }

    auto window = std::make_shared<XER::UI::Core::Window>();
    if (!window->Create("XBrowser", 1280, 800)) {
        std::fprintf(stderr, "window creation failed\n");
        XER::Cef::Shutdown();
        app.Shutdown();
        return 1;
    }
    window->SetTheme(XER::UI::Theme::Variant::Dark);
    window->SetSizeLimits(480, 320, 0, 0);

    auto browser = std::make_shared<XBrowserWidget>(window.get(), urls,
                                                    cef_error, cef_ok);
    window->SetContent(browser);
    window->SetFrameCallback([browser] { browser->TickTree(0.033); },
                             33);
    window->Show();

    const int rc = app.Run();

    // Native children die while CEF is still up: close before shutting
    // the runtimes down.
    browser->Teardown();
    XER::Cef::Shutdown();
    app.Shutdown();
    return rc;
}

} // namespace XER::Demo

#else   // !_WIN32 — HWND embedding is Windows-only here.

namespace XER::Demo {
int RunXBrowser(const std::vector<std::string>&) {
    std::fputs("XBrowser is Windows-only\n", stderr);
    return 1;
}
} // namespace XER::Demo

#endif  // _WIN32

namespace {

void PrintUsage() {
    std::printf(
        "Usage:\n"
        "  XBrowser [url ...]\n"
        "\n"
        "Each argument opens a tab; no arguments opens the home page.\n"
        "Ctrl+N new tab, F5 reload, middle-click closes a tab.\n");
}

#if defined(_WIN32)
std::vector<std::string> CommandLineArgsUtf8() {
    std::vector<std::string> out;
    int wargc = 0;
    LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
    if (wargv == nullptr) {
        return out;
    }
    out.reserve(static_cast<size_t>(wargc));
    for (int i = 0; i < wargc; ++i) {
        out.push_back(XER::Helper::String::WideToUtf8(wargv[i]));
    }
    LocalFree(wargv);
    return out;
}
#endif

} // namespace

int main(int, char**) {
    std::vector<std::string> urls;
#if defined(_WIN32)
    const std::vector<std::string> args = CommandLineArgsUtf8();
#else
    const std::vector<std::string> args;
#endif
    for (size_t i = 1; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (a == "-h" || a == "--help") {
            PrintUsage();
            return 0;
        }
        urls.push_back(a);
    }
    return XER::Demo::RunXBrowser(urls);
}
