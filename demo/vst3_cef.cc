// vst3_cef.cc — see vst3_cef.h.
//
// src/ui edition: window chrome, widgets, theming and the event loop
// come from the XER UI framework (UI::Core::Application / Window). The
// demo contributes one root widget — Vst3CefDemoWidget — whose layout
// reserves two pane rects below the toolbar. Both panes are real child
// HWNDs of the window (they draw over the D2D surface): the left hosts
// the plugin's IPlugView via Vst3HostInterface, the right hosts a
// CefHost Chromium browser. The HWND-placement mechanics mirror
// FxRackWidget::SyncEditorFrame — widget bounds are DIPs, HWND rects
// are client-area pixels, so a GetDpiForWindow scale factor converts.
//
// CefHost events fire on CEF's UI thread; the callbacks only push into
// a shared pending-state struct which Tick() drains on the UI thread —
// no widget is ever touched off-thread.
#include "vst3_cef.h"

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
#include <commdlg.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>

#include "audio/plugin_wrappers/vst3_host_interface.h"
#include "audio/plugin_wrappers/vst3_plugin_scanner.h"
#include "cef/cef_host.h"
#include "content/audio/audio_player.h"
#include "cef/cef_runtime.h"

#include "helper/string_utils.h"
#include "ui/core/application.h"
#include "ui/core/window.h"
#include "ui/widgets/widget.h"
#include "ui/widgets/button.h"
#include "ui/widgets/label.h"
#include "ui/widgets/select.h"
#include "ui/widgets/text_input.h"
#include "ui/rendering/renderer.h"

namespace XER::Demo {
namespace {

// Landing page for the CEF pane when no --url is given.
const char kWelcomeHtml[] = R"HTML(
<!doctype html><html><head><meta charset="utf-8"><title>CEF View</title><style>
body{background:#17181c;color:#d7d7de;font-family:Segoe UI,sans-serif;
     display:flex;align-items:center;justify-content:center;height:100vh;margin:0}
div{max-width:520px;text-align:center;line-height:1.5}
h1{color:#7aa2f7;font-weight:600}
code{background:#26272e;padding:2px 6px;border-radius:4px}
</style></head><body><div>
<h1>CEF View</h1>
<p>Chromium browser embedded via <code>XER::Cef::CefHost</code>.
Use the address bar above to navigate.</p>
<p>The pane on the left hosts a VST3 plugin's native editor view
(<code>IPlugView</code> via <code>Vst3HostInterface</code>).</p>
<p><button onclick="tone()">Play a test tone</button> — WebAudio renders
through Chromium's own output.</p>
</div>
<script>
let ac;
function tone(){
  ac = ac || new (window.AudioContext||window.webkitAudioContext)();
  const o = ac.createOscillator(), g = ac.createGain();
  o.frequency.value = 440; g.gain.value = 0.15;
  o.connect(g).connect(ac.destination); o.start();
  setTimeout(()=>o.stop(), 1500);
}
</script></body></html>
)HTML";

// Slim strip showing the loaded VST3 chain: one chip per plugin, click
// selects which editor the pane shows, × removes the plugin from the
// chain. Same hand-drawn approach as the XBrowser tab strip — no glyph
// or icon dependencies.
class ChainStrip final : public UI::Widgets::Widget {
public:
    void SetItems(std::vector<std::string> names) {
        names_ = std::move(names);
        Invalidate();
    }
    void SetBypassed(std::vector<char> flags) {
        bypassed_ = std::move(flags);
        Invalidate();
    }
    void SetActive(int index) { active_ = index; Invalidate(); }
    void SetOnSelect(std::function<void(int)> cb) { on_select_ = std::move(cb); }
    void SetOnClose(std::function<void(int)> cb) { on_close_ = std::move(cb); }
    void SetOnToggle(std::function<void(int)> cb) { on_toggle_ = std::move(cb); }

    UI::Layout::Size GetMinSize() const override { return {60.0f, 26.0f}; }
    UI::Layout::Size GetPreferredSize() const override { return {400.0f, 26.0f}; }
    UI::Layout::Size GetMaxSize() const override { return {100000.0f, 26.0f}; }

    void Layout(const UI::Layout::Rect& bounds) override {
        SetBounds(bounds);
        const int count = static_cast<int>(names_.size());
        float w = count > 0
                      ? bounds.width / static_cast<float>(count)
                      : 0.0f;
        w = std::min(kChipMaxW, std::max(kChipMinW, w));
        chip_rects_.clear();
        float x = bounds.x;
        for (int i = 0; i < count; ++i) {
            chip_rects_.push_back({x, bounds.y, w, bounds.height});
            x += w + kGap;
        }
    }
    using Widget::Layout;

    void Render(UI::Rendering::Renderer& renderer,
                const UI::Theme::Tokens& tokens) override {
        const UI::Rendering::TextStyle style{tokens.foreground, 12.0f, 400};
        const UI::Rendering::TextStyle dim_style{tokens.muted, 12.0f, 400};
        for (size_t i = 0; i < chip_rects_.size(); ++i) {
            const auto& r = chip_rects_[i];
            const bool active = static_cast<int>(i) == active_;
            const bool hovered = static_cast<int>(i) == hovered_;
            const bool bypassed =
                i < bypassed_.size() && bypassed_[i] != 0;
            renderer.FillRect(
                r, active ? tokens.surface : (hovered ? tokens.hover
                                                      : tokens.background),
                6.0f);
            renderer.StrokeRect(
                r, bypassed ? tokens.muted
                            : (active ? tokens.accent : tokens.border),
                1.0f, 6.0f);
            const UI::Layout::Rect text_rect{
                r.x + 8.0f, r.y, r.width - 34.0f, r.height};
            renderer.PushClip(text_rect);
            renderer.DrawText(bypassed ? names_[i] + " (off)" : names_[i],
                              text_rect,
                              bypassed ? dim_style : style);
            renderer.PopClip();
            const UI::Layout::Rect cr = CloseRect(r);
            if (close_hovered_ == static_cast<int>(i)) {
                renderer.FillRect(cr, tokens.hover, 4.0f);
            }
            const float cx = cr.x + 5.0f, cy = cr.y + 4.0f, cs = 7.0f;
            renderer.DrawLine({cx, cy}, {cx + cs, cy + cs}, tokens.muted, 1.5f);
            renderer.DrawLine({cx + cs, cy}, {cx, cy + cs}, tokens.muted, 1.5f);
        }
    }

    bool HandleEvent(const UI::Widgets::Event& event) override {
        using Type = UI::Widgets::Event::Type;
        if (event.type == Type::MouseMove) {
            const int close_hit = HitClose(event.position);
            close_hovered_ = close_hit;
            hovered_ = close_hit >= 0 ? close_hit : HitIndex(event.position);
            Invalidate();
            return false;
        }
        if (event.type == Type::MouseDown) {
            const int close_hit = HitClose(event.position);
            if (close_hit >= 0 && on_close_) {
                on_close_(close_hit);
                return true;
            }
            const int hit = HitIndex(event.position);
            if (hit >= 0 &&
                event.mouse_button == UI::Widgets::MouseButton::Right &&
                on_toggle_) {
                on_toggle_(hit);
                return true;
            }
            if (hit >= 0 && on_select_) {
                on_select_(hit);
                return true;
            }
        }
        return false;
    }

private:
    static constexpr float kChipMinW = 90.0f;
    static constexpr float kChipMaxW = 220.0f;
    static constexpr float kGap = 4.0f;

    UI::Layout::Rect CloseRect(const UI::Layout::Rect& r) const {
        return {r.x + r.width - 24.0f,
                r.y + (r.height - 18.0f) * 0.5f, 18.0f, 18.0f};
    }
    int HitIndex(const UI::Layout::Point& p) const {
        for (size_t i = 0; i < chip_rects_.size(); ++i) {
            if (chip_rects_[i].Contains(p)) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }
    int HitClose(const UI::Layout::Point& p) const {
        for (size_t i = 0; i < chip_rects_.size(); ++i) {
            if (CloseRect(chip_rects_[i]).Contains(p)) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    std::vector<std::string> names_;
    std::vector<char> bypassed_;
    std::vector<UI::Layout::Rect> chip_rects_;
    int active_ = -1;
    int hovered_ = -1;
    int close_hovered_ = -1;
    std::function<void(int)> on_select_;
    std::function<void(int)> on_close_;
    std::function<void(int)> on_toggle_;
};

// Thread-safe handoff from CEF's UI thread to the app UI thread.
// Heap-allocated and shared: CefHost may outlive the widget (its Close
// is async), so events must never write into the widget itself.
struct CefPending {
    std::mutex mutex;
    std::optional<std::string> url;
    std::optional<std::string> title;
    std::optional<bool> loading;
    bool closed = false;
};

class Vst3CefDemoWidget final : public UI::Widgets::Widget {
public:
    Vst3CefDemoWidget(UI::Core::Window* window,
                      const Vst3CefDemoOptions& options,
                      std::string cef_error, bool cef_available)
        : window_(window)
        , parent_hwnd_(window != nullptr ? window->GetHWND() : nullptr)
        , options_(options)
        , cef_error_(std::move(cef_error))
        , cef_available_(cef_available) {
        player_ = std::make_unique<Content::Audio::AudioPlayer>();
        // Adopts the endpoint mix format internally; GetSampleRate()
        // reports the rate blocks are actually delivered at, which is
        // also the rate plugin hosts are initialized with.
        audio_rate_ = player_->Initialize(
            static_cast<int>(options_.sample_rate), 2)
                          ? player_->GetSampleRate()
                          : static_cast<int>(options_.sample_rate);
        player_->SetBlockProcessor(
            [this](std::vector<float>& block) { ProcessAudioBlock(block); });
        player_->SetStateCallback([this](Content::Audio::PlaybackState) {
            audio_state_dirty_ = true;
        });

        chain_strip_ = std::make_shared<ChainStrip>();
        chain_strip_->SetOnSelect([this](int i) { SelectChain(i); });
        chain_strip_->SetOnClose([this](int i) { RemoveChain(i); });
        chain_strip_->SetOnToggle([this](int i) { ToggleChainBypass(i); });

        audio_path_input_ = std::make_shared<UI::Widgets::TextInput>(
            options_.audio_path);
        audio_path_input_->SetPlaceholder("D:\\MusicMy\\track.mp3");
        audio_browse_button_ = std::make_shared<UI::Widgets::Button>(
            "Audio...", UI::Widgets::ButtonStyle::Secondary);
        audio_play_button_ = std::make_shared<UI::Widgets::Button>(
            "Play", UI::Widgets::ButtonStyle::Primary);
        audio_stop_button_ = std::make_shared<UI::Widgets::Button>(
            "Stop", UI::Widgets::ButtonStyle::Outline);
        audio_time_ = std::make_shared<UI::Widgets::Label>("--:-- / --:--", true);

        plugin_select_ = std::make_shared<UI::Widgets::Select>(
            std::vector<std::string>{}, -1);
        plugin_select_->SetPlaceholder("Scan or paste a .vst3 path...");
        plugin_path_input_ = std::make_shared<UI::Widgets::TextInput>();
        plugin_path_input_->SetPlaceholder("C:\\...\\Plugin.vst3");
        browse_button_ =
            std::make_shared<UI::Widgets::Button>("Browse", UI::Widgets::ButtonStyle::Secondary);
        scan_button_ =
            std::make_shared<UI::Widgets::Button>("Scan", UI::Widgets::ButtonStyle::Secondary);
        load_button_ =
            std::make_shared<UI::Widgets::Button>("Load", UI::Widgets::ButtonStyle::Primary);
        unload_button_ =
            std::make_shared<UI::Widgets::Button>("Unload", UI::Widgets::ButtonStyle::Outline);

        back_button_ =
            std::make_shared<UI::Widgets::Button>("<", UI::Widgets::ButtonStyle::Outline);
        fwd_button_ =
            std::make_shared<UI::Widgets::Button>(">", UI::Widgets::ButtonStyle::Outline);
        reload_button_ =
            std::make_shared<UI::Widgets::Button>("R", UI::Widgets::ButtonStyle::Outline);
        devtools_button_ =
            std::make_shared<UI::Widgets::Button>("DevTools", UI::Widgets::ButtonStyle::Outline);
        url_input_ = std::make_shared<UI::Widgets::TextInput>(options_.url);
        url_input_->SetPlaceholder("https://...");
        go_button_ =
            std::make_shared<UI::Widgets::Button>("Go", UI::Widgets::ButtonStyle::Primary);
        status_ = std::make_shared<UI::Widgets::Label>("", true);

        plugin_select_->SetOnChanged([this](int index) {
            if (index >= 0 &&
                index < static_cast<int>(scanned_plugins_.size())) {
                plugin_path_input_->SetValue(
                    scanned_plugins_[static_cast<size_t>(index)].path);
            }
        });
        browse_button_->SetOnClick([this] { BrowsePlugin(); });
        scan_button_->SetOnClick([this] { ScanPlugins(); });
        load_button_->SetOnClick([this] {
            const std::string path = plugin_path_input_->GetValue();
            if (path.empty()) {
                SetStatus("enter a .vst3 path first");
            } else {
                LoadPlugin(path);
            }
        });
        unload_button_->SetOnClick([this] { UnloadPlugin(); });
        plugin_path_input_->SetOnSubmit([this] {
            const std::string path = plugin_path_input_->GetValue();
            if (!path.empty()) {
                LoadPlugin(path);
            }
        });

        back_button_->SetOnClick([this] { cef_.GoBack(); });
        fwd_button_->SetOnClick([this] { cef_.GoForward(); });
        reload_button_->SetOnClick([this] { cef_.Reload(); });
        devtools_button_->SetOnClick([this] { cef_.ShowDevTools(); });
        go_button_->SetOnClick([this] { Navigate(); });
        url_input_->SetOnSubmit([this] { Navigate(); });

        audio_browse_button_->SetOnClick([this] { BrowseAudio(); });
        audio_play_button_->SetOnClick([this] { ToggleAudioPlayback(); });
        audio_stop_button_->SetOnClick([this] { player_->Stop(); });
        audio_path_input_->SetOnSubmit([this] {
            LoadAudioFile(audio_path_input_->GetValue());
        });

        AddChild(chain_strip_);
        AddChild(audio_path_input_);
        AddChild(audio_browse_button_);
        AddChild(audio_play_button_);
        AddChild(audio_stop_button_);
        AddChild(audio_time_);
        AddChild(plugin_select_);
        AddChild(plugin_path_input_);
        AddChild(browse_button_);
        AddChild(scan_button_);
        AddChild(load_button_);
        AddChild(unload_button_);
        AddChild(back_button_);
        AddChild(fwd_button_);
        AddChild(reload_button_);
        AddChild(devtools_button_);
        AddChild(url_input_);
        AddChild(go_button_);
        AddChild(status_);

        if (!options_.plugin_path.empty()) {
            plugin_path_input_->SetValue(options_.plugin_path);
            LoadPlugin(options_.plugin_path);
        } else {
            SetStatus("Ready — load a .vst3 plugin and/or navigate the "
                      "browser");
        }
        if (!options_.audio_path.empty()) {
            LoadAudioFile(options_.audio_path);
        }
    }

    ~Vst3CefDemoWidget() override { Teardown(); }

    // Detaches the plug-view and closes the browser. Called by the demo
    // before Cef::Shutdown and again from the destructor — idempotent.
    void Teardown() {
        if (torn_down_) {
            return;
        }
        torn_down_ = true;
        // Stop the render thread before dropping the chain — the block
        // processor calls into live plugin instances.
        player_->SetBlockProcessor(nullptr);
        player_->Stop();
        DetachPlugin();
        {
            std::lock_guard<std::mutex> lk(chain_mutex_);
            chain_.clear();
            chain_paths_.clear();
            chain_bypass_.clear();
            chain_active_ = -1;
        }
        cef_browser_ = false;
        cef_.Close();
    }

    void Render(UI::Rendering::Renderer& renderer,
                const UI::Theme::Tokens& tokens) override {
        renderer.FillRect(GetBounds(), tokens.background, 0.0f);
        for (const UI::Layout::Rect* pane : {&editor_rect_, &browser_rect_}) {
            renderer.FillRect(*pane, tokens.surface, tokens.radius_medium);
            renderer.StrokeRect(*pane, tokens.border, 1.0f,
                                tokens.radius_medium);
        }
        // The native plug-view/browser HWNDs cover their pane when
        // alive; only placeholders are drawn into empty panes.
        if (!editor_attached_ && !editor_attach_pending_) {
            DrawCentered(renderer,
                         "VST3 pane — no plugin editor loaded",
                         editor_rect_, tokens);
        }
        if (!cef_browser_ && !cef_error_.empty()) {
            DrawCentered(renderer, "CEF: " + cef_error_, browser_rect_,
                         tokens);
        }
    }

    void Layout(const UI::Layout::Rect& bounds) override {
        SetBounds(bounds);
        const auto& tokens = CachedLayoutTokens();
        const float pad = tokens.spacing_medium;
        const float gap = tokens.spacing_small;
        const float row = 30.0f;
        const float x0 = bounds.x + pad;
        const float w = bounds.width - 2.0f * pad;
        float y = bounds.y + pad;

        // Row 1 — VST3 controls.
        float x = x0;
        plugin_select_->Layout(UI::Layout::Rect{x, y, 200.0f, row}, tokens);
        x += 200.0f + gap;
        float xr = x0 + w;
        auto place_right = [&](const std::shared_ptr<UI::Widgets::Widget>& b,
                               float width) {
            xr -= width;
            b->Layout(UI::Layout::Rect{xr, y, width, row}, tokens);
            xr -= gap;
        };
        place_right(unload_button_, 72.0f);
        place_right(load_button_, 64.0f);
        place_right(scan_button_, 60.0f);
        place_right(browse_button_, 72.0f);
        plugin_path_input_->Layout(UI::Layout::Rect{x, y, xr - x, row}, tokens);
        y += row + gap;

        // Row 2 — CEF controls.
        back_button_->Layout(UI::Layout::Rect{x0, y, 30.0f, row}, tokens);
        fwd_button_->Layout(UI::Layout::Rect{x0 + 34.0f, y, 30.0f, row}, tokens);
        reload_button_->Layout(UI::Layout::Rect{x0 + 68.0f, y, 30.0f, row}, tokens);
        devtools_button_->Layout(UI::Layout::Rect{x0 + 102.0f, y, 80.0f, row}, tokens);
        go_button_->Layout(UI::Layout::Rect{x0 + w - 48.0f, y, 48.0f, row}, tokens);
        url_input_->Layout(UI::Layout::Rect{
            x0 + 102.0f + 80.0f + gap, y,
            w - 102.0f - 80.0f - 48.0f - 2.0f * gap, row}, tokens);
        y += row + gap;

        // Row 3 — audio file + transport (file -> VST3 chain -> output).
        const float pane_w = (w - gap) * 0.5f;
        float axr = x0 + w;
        auto place_audio_right = [&](const std::shared_ptr<UI::Widgets::Widget>& b,
                                     float width) {
            axr -= width;
            b->Layout(UI::Layout::Rect{axr, y, width, row}, tokens);
            axr -= gap;
        };
        place_audio_right(audio_time_, 200.0f);
        place_audio_right(audio_stop_button_, 56.0f);
        place_audio_right(audio_play_button_, 64.0f);
        place_audio_right(audio_browse_button_, 72.0f);
        audio_path_input_->Layout(
            UI::Layout::Rect{x0, y, axr - x0, row}, tokens);
        y += row + gap;

        // Chain strip over the left (editor) pane only.
        chain_strip_->Layout(UI::Layout::Rect{x0, y, pane_w, 26.0f}, tokens);
        y += 26.0f + pad;

        // Content: two equal panes above the status line.
        const float status_h = 22.0f;
        const float content_h =
            bounds.Bottom() - pad - status_h - y;
        editor_rect_ = UI::Layout::Rect{x0, y, pane_w, content_h};
        browser_rect_ =
            UI::Layout::Rect{x0 + pane_w + gap, y, pane_w, content_h};
        status_->Layout(UI::Layout::Rect{
            x0, bounds.Bottom() - pad - status_h + 3.0f, w, status_h},
                        tokens);

        SyncEditorFrame();
        SyncBrowserFrame();
    }
    using Widget::Layout;

    bool HandleEvent(const UI::Widgets::Event&) override { return false; }

    UI::Layout::Size GetMinSize() const override { return {720.0f, 480.0f}; }
    UI::Layout::Size GetPreferredSize() const override {
        return {1180.0f, 720.0f};
    }
    UI::Layout::Size GetMaxSize() const override {
        return {100000.0f, 100000.0f};
    }

    // Driven by Window::SetFrameCallback via TickTree.
    void Tick(double dt_seconds) override {
        (void)dt_seconds;
        PumpCefEvents();
        UpdateAudioWidgets();
        // Keep the plug-view glued to its rect (window moves between
        // layouts, e.g. while the pane is re-shown).
        SyncEditorFrame();
    }

private:
    void DrawCentered(UI::Rendering::Renderer& renderer,
                      const std::string& text, const UI::Layout::Rect& rect,
                      const UI::Theme::Tokens& tokens) const {
        if (rect.width <= 0.0f || rect.height <= 0.0f) {
            return;
        }
        const UI::Rendering::TextStyle style{tokens.muted, 14.0f, 400};
        const UI::Layout::Size size = renderer.MeasureText(text, style);
        renderer.DrawText(
            text,
            UI::Layout::Rect{rect.x + (rect.width - size.width) * 0.5f,
                         rect.y + (rect.height - size.height) * 0.5f,
                         size.width, size.height},
            style);
    }

    // DIP (widget bounds) -> physical pixel scale for HWND placement.
    float DpiScale() const {
        if (parent_hwnd_ == nullptr) {
            return 1.0f;
        }
        const UINT dpi = GetDpiForWindow(static_cast<HWND>(parent_hwnd_));
        return dpi != 0 ? static_cast<float>(dpi) / 96.0f : 1.0f;
    }

    void SetStatus(const std::string& text) {
        status_->SetText(text);
    }

    // ---- VST3 chain -------------------------------------------------

    Audio::Plugin::Vst3HostInterface* ActiveHost() {
        return chain_active_ >= 0 &&
                       chain_active_ < static_cast<int>(chain_.size())
                   ? chain_[static_cast<size_t>(chain_active_)].get()
                   : nullptr;
    }

    void RefreshChainStrip() {
        std::vector<std::string> names;
        names.reserve(chain_.size());
        for (const auto& host : chain_) {
            std::string name = host->GetPluginName();
            names.push_back(name.empty() ? "plugin" : name);
        }
        chain_strip_->SetItems(std::move(names));
        {
            std::lock_guard<std::mutex> lk(chain_mutex_);
            chain_strip_->SetBypassed(
                std::vector<char>(chain_bypass_.begin(), chain_bypass_.end()));
        }
        chain_strip_->SetActive(chain_active_);
    }

    void LoadPlugin(const std::string& path) {
        // Already in the chain? Just bring its editor forward.
        for (size_t i = 0; i < chain_.size(); ++i) {
            if (chain_paths_[i] == path) {
                SelectChain(static_cast<int>(i));
                return;
            }
        }
        auto host = std::make_unique<Audio::Plugin::Vst3HostInterface>();
        host->Initialize(audio_rate_, options_.max_block_size);
        if (!host->LoadPlugin(path)) {
            const std::string detail = host->GetLastLoadError();
            std::fprintf(stderr, "VST3 load failed: %s\n",
                         detail.empty() ? path.c_str() : detail.c_str());
            SetStatus("VST3 load failed: " +
                      (detail.empty() ? path : detail));
            return;
        }
        const std::string name = host->GetPluginName();
        const bool has_resources = !host->GetResourcesPath().empty();
        {
            std::lock_guard<std::mutex> lk(chain_mutex_);
            chain_.push_back(std::move(host));
            chain_paths_.push_back(path);
            chain_bypass_.push_back(0);
        }
        chain_active_ = -1;  // force SelectChain to attach the new editor
        SelectChain(static_cast<int>(chain_.size()) - 1);
        RefreshChainStrip();
        std::string status = "Chain " + std::to_string(chain_.size()) +
                             ": '" + (name.empty() ? path : name) + "'";
        if (has_resources) {
            status += " (+Resources)";
        }
        SetStatus(status);
        Invalidate();
        RequestLayout();
    }

    // Makes chain_[index] the visible plugin: detaches the currently
    // attached editor and queues this one's view for attach.
    void SelectChain(int index) {
        if (index < 0 || index >= static_cast<int>(chain_.size()) ||
            index == chain_active_) {
            return;
        }
        DetachPlugin();
        chain_active_ = index;
        chain_strip_->SetActive(index);
        Audio::Plugin::Vst3HostInterface* host = ActiveHost();
        if (host->GetBackendMode() != "builtin" && host->HasPluginEditor()) {
            std::string error;
            if (host->CreatePluginEditor(&error)) {
                editor_attach_pending_ = true;
            } else {
                SetStatus("Editor create failed: " +
                          (error.empty() ? std::string("unknown") : error));
            }
        } else {
            SetStatus("'" + host->GetPluginName() +
                      "' exposes no editor view");
        }
        Invalidate();
        RequestLayout();
    }

    void ToggleChainBypass(int index) {
        {
            std::lock_guard<std::mutex> lk(chain_mutex_);
            if (index < 0 ||
                index >= static_cast<int>(chain_bypass_.size())) {
                return;
            }
            chain_bypass_[static_cast<size_t>(index)] =
                chain_bypass_[static_cast<size_t>(index)] == 0 ? 1 : 0;
        }
        RefreshChainStrip();
        std::lock_guard<std::mutex> lk(chain_mutex_);
        SetStatus(std::string(chain_bypass_[static_cast<size_t>(index)] != 0
                                  ? "FX bypassed: "
                                  : "FX enabled: ") +
                  chain_[static_cast<size_t>(index)]->GetPluginName());
        Invalidate();
    }

    void RemoveChain(int index) {
        if (index < 0 || index >= static_cast<int>(chain_.size())) {
            return;
        }
        if (index == attached_index_ && (editor_attached_ || editor_attach_pending_)) {
            DetachPlugin();
        }
        {
            std::lock_guard<std::mutex> lk(chain_mutex_);
            chain_.erase(chain_.begin() + index);
            chain_paths_.erase(chain_paths_.begin() + index);
            chain_bypass_.erase(chain_bypass_.begin() + index);
        }
        if (chain_active_ == index) {
            chain_active_ = -1;
        } else if (chain_active_ > index) {
            --chain_active_;
        }
        if (attached_index_ > index) {
            --attached_index_;
        }
        RefreshChainStrip();
        if (chain_active_ < 0 && !chain_.empty()) {
            SelectChain(static_cast<int>(chain_.size()) - 1);
        }
        SetStatus("plugin removed — chain: " +
                  std::to_string(chain_.size()));
        Invalidate();
        RequestLayout();
    }

    void UnloadPlugin() {
        DetachPlugin();
        {
            std::lock_guard<std::mutex> lk(chain_mutex_);
            chain_.clear();
            chain_paths_.clear();
            chain_bypass_.clear();
        }
        chain_active_ = -1;
        RefreshChainStrip();
        SetStatus("chain cleared");
        Invalidate();
        RequestLayout();
    }

    void DetachPlugin() {
        if (editor_attached_ || editor_attach_pending_) {
            if (attached_index_ >= 0 &&
                attached_index_ < static_cast<int>(chain_.size())) {
                chain_[static_cast<size_t>(attached_index_)]
                    ->RemovePluginEditor();
            }
        }
        editor_attached_ = false;
        editor_attach_pending_ = false;
        attached_index_ = -1;
        last_editor_frame_ = {};
        Invalidate();
    }

    void ScanPlugins() {
        SetStatus("scanning VST3 search roots...");
        scanned_plugins_ = Audio::Plugin::ScanVst3Plugins(
            Audio::Plugin::DefaultVst3SearchRoots());
        std::vector<std::string> options;
        options.reserve(scanned_plugins_.size());
        for (const auto& p : scanned_plugins_) {
            std::string label = p.name.empty() ? p.path : p.name;
            if (!p.vendor.empty()) {
                label += " — " + p.vendor;
            }
            options.push_back(std::move(label));
        }
        plugin_select_->SetOptions(std::move(options));
        plugin_select_->SetSelectedIndex(-1);
        SetStatus(scanned_plugins_.empty()
                      ? "no .vst3 modules found in search roots"
                      : std::to_string(scanned_plugins_.size()) +
                            " plugin(s) found — pick one or paste a path");
    }

    void BrowsePlugin() {
        wchar_t file[MAX_PATH] {};
        OPENFILENAMEW ofn {};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = static_cast<HWND>(parent_hwnd_);
        ofn.lpstrFile = file;
        ofn.nMaxFile = MAX_PATH;
        ofn.lpstrFilter =
            L"VST3 plugins (*.vst3)\0*.vst3\0All files (*.*)\0*.*\0";
        // Bundles are directories — navigate inside and pick
        // Contents\<arch>-win\*.vst3; loose .vst3 files pick directly.
        const char* cpf = std::getenv("CommonProgramFiles");
        const std::wstring initial =
            cpf != nullptr
                ? XER::Helper::String::Utf8ToWide(
                      std::string(cpf) + "\\VST3")
                : L"";
        if (!initial.empty()) {
            ofn.lpstrInitialDir = initial.c_str();
        }
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (GetOpenFileNameW(&ofn) != 0) {
            const std::string path =
                XER::Helper::String::WideToUtf8(file);
            plugin_path_input_->SetValue(path);
            LoadPlugin(path);
        }
    }

    // ---- Audio: file -> VST3 chain -> WASAPI output ------------------

    void BrowseAudio() {
        wchar_t file[MAX_PATH] {};
        OPENFILENAMEW ofn {};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = static_cast<HWND>(parent_hwnd_);
        ofn.lpstrFile = file;
        ofn.nMaxFile = MAX_PATH;
        ofn.lpstrFilter =
            L"Audio (*.wav;*.mp3;*.flac;*.ogg;*.m4a)\0"
            L"*.wav;*.mp3;*.flac;*.ogg;*.m4a\0All files (*.*)\0*.*\0";
        const wchar_t* initial = L"D:\\MusicMy";
        if (GetFileAttributesW(initial) != INVALID_FILE_ATTRIBUTES) {
            ofn.lpstrInitialDir = initial;
        }
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (GetOpenFileNameW(&ofn) != 0) {
            const std::string path = XER::Helper::String::WideToUtf8(file);
            audio_path_input_->SetValue(path);
            LoadAudioFile(path);
        }
    }

    void LoadAudioFile(const std::string& path) {
        if (path.empty()) {
            SetStatus("enter an audio file path first");
            return;
        }
        std::string error;
        if (!player_->LoadFromFile(path, &error)) {
            SetStatus("audio load failed: " +
                      (error.empty() ? path : error));
            return;
        }
        SetStatus("audio ready: " + path + " (" +
                  FormatTime(player_->GetDuration()) + ")");
        audio_play_button_->SetText("Play");
    }

    void ToggleAudioPlayback() {
        if (!player_->IsLoaded()) {
            LoadAudioFile(audio_path_input_->GetValue());
            if (!player_->IsLoaded()) {
                return;
            }
        }
        if (player_->GetState() == Content::Audio::PlaybackState::kPlaying) {
            player_->Pause();
        } else {
            player_->Play();
        }
    }

    static std::string FormatTime(double seconds) {
        if (seconds < 0.0) {
            seconds = 0.0;
        }
        const int total = static_cast<int>(seconds + 0.5);
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%d:%02d", total / 60, total % 60);
        return buf;
    }

    // Runs on the audio thread: deinterleave -> every plugin in the
    // chain -> reinterleave. Each channel goes through the same plugin
    // instance (dual-mono processing).
    void ProcessAudioBlock(std::vector<float>& block) {
        const int channels = std::max(1, player_->GetChannelCount());
        const size_t frames = block.size() / static_cast<size_t>(channels);
        if (frames == 0) {
            return;
        }
        audio_chans_.resize(static_cast<size_t>(channels));
        for (int ch = 0; ch < channels; ++ch) {
            auto& buf = audio_chans_[static_cast<size_t>(ch)];
            buf.resize(frames);
            for (size_t f = 0; f < frames; ++f) {
                buf[f] = block[f * static_cast<size_t>(channels) +
                             static_cast<size_t>(ch)];
            }
        }
        float in_peak = 0.0f;
        for (const auto& buf : audio_chans_) {
            for (float s : buf) {
                in_peak = std::max(in_peak, std::fabs(s));
            }
        }
        {
            std::lock_guard<std::mutex> lk(chain_mutex_);
            constexpr size_t kMaxChannels = 8;
            if (channels <= static_cast<int>(kMaxChannels)) {
                const float* ins[kMaxChannels];
                float* outs[kMaxChannels];
                for (int ch = 0; ch < channels; ++ch) {
                    ins[ch] = audio_chans_[static_cast<size_t>(ch)].data();
                    outs[ch] = audio_chans_[static_cast<size_t>(ch)].data();
                }
                for (size_t i = 0; i < chain_.size(); ++i) {
                    if (i < chain_bypass_.size() && chain_bypass_[i] != 0) {
                        continue;
                    }
                    chain_[i]->ProcessChannels(
                        ins, outs, channels, static_cast<uint32_t>(frames));
                }
            }
        }
        float out_peak = 0.0f;
        for (const auto& buf : audio_chans_) {
            for (float s : buf) {
                out_peak = std::max(out_peak, std::fabs(s));
            }
        }
        audio_in_peak_.store(in_peak, std::memory_order_relaxed);
        audio_out_peak_.store(out_peak, std::memory_order_relaxed);
        for (int ch = 0; ch < channels; ++ch) {
            const auto& buf = audio_chans_[static_cast<size_t>(ch)];
            for (size_t f = 0; f < frames; ++f) {
                block[f * static_cast<size_t>(channels) +
                      static_cast<size_t>(ch)] = buf[f];
            }
        }
    }

    void UpdateAudioWidgets() {
        if (++audio_meter_tick_ >= 15) {
            audio_meter_tick_ = 0;
            char meter[128];
            std::snprintf(
                meter, sizeof(meter), "%s / %s  in %.2f out %.2f",
                FormatTime(player_->GetCurrentTime()).c_str(),
                FormatTime(player_->GetDuration()).c_str(),
                audio_in_peak_.load(std::memory_order_relaxed),
                audio_out_peak_.load(std::memory_order_relaxed));
            std::string text = meter;
            // A chain that swallows live input looks like "no sound" —
            // surface the bypass path instead of letting it look broken.
            if (player_->GetState() ==
                    Content::Audio::PlaybackState::kPlaying &&
                audio_in_peak_.load(std::memory_order_relaxed) > 0.01f &&
                audio_out_peak_.load(std::memory_order_relaxed) < 0.001f &&
                !chain_.empty()) {
                text += "   [silent: right-click chip to bypass]";
            }
            audio_time_->SetText(text);
        }
        if (audio_state_dirty_.exchange(false)) {
            const bool playing =
                player_->GetState() ==
                Content::Audio::PlaybackState::kPlaying;
            audio_play_button_->SetText(playing ? "Pause" : "Play");
            if (!chain_.empty()) {
                SetStatus(std::string("audio ") +
                          (playing ? "playing" : "stopped") +
                          " -> chain of " + std::to_string(chain_.size()) +
                          " -> output");
            }
        }
    }

    // Attach/move the plug-view to editor_rect_ (clamped to the visible
    // viewport — a child HWND is not clipped by the renderer).
    void SyncEditorFrame() {
        if (parent_hwnd_ == nullptr || torn_down_) {
            return;
        }
        const UI::Layout::Rect visible =
            editor_rect_.Intersect(GetVisibleRect());
        const bool has_rect = visible.width > 1.0f && visible.height > 1.0f;
        const UI::Layout::Rect frame =
            has_rect ? visible
                     : UI::Layout::Rect{GetVisibleRect().x,
                                    GetVisibleRect().y, 1.0f, 1.0f};
        const float s = DpiScale();
        const int l = static_cast<int>(frame.x * s);
        const int t = static_cast<int>(frame.y * s);
        const int r = static_cast<int>((frame.x + frame.width) * s);
        const int b = static_cast<int>((frame.y + frame.height) * s);

        if (editor_attach_pending_) {
            if (!has_rect && editor_rect_.width <= 1.0f) {
                return;   // pane not laid out yet
            }
            Audio::Plugin::Vst3HostInterface* host = ActiveHost();
            if (host == nullptr) {
                editor_attach_pending_ = false;
                return;
            }
            std::string error;
            if (host->AttachPluginEditor(parent_hwnd_, l, t, r, b,
                                         &error)) {
                editor_attached_ = true;
                editor_attach_pending_ = false;
                attached_index_ = chain_active_;
                last_editor_frame_ = {static_cast<float>(l),
                                      static_cast<float>(t),
                                      static_cast<float>(r),
                                      static_cast<float>(b)};
                const std::string name = host->GetPluginName();
                SetStatus("VST3 editor attached: " +
                          (name.empty()
                               ? plugin_path_input_->GetValue()
                               : name));
            } else {
                editor_attach_pending_ = false;
                SetStatus("Editor attach failed: " +
                          (error.empty() ? std::string("unknown")
                                         : error));
            }
            Invalidate();
            return;
        }
        if (editor_attached_ && attached_index_ >= 0 &&
            attached_index_ < static_cast<int>(chain_.size())) {
            const UI::Layout::Rect scaled{static_cast<float>(l),
                                      static_cast<float>(t),
                                      static_cast<float>(r),
                                      static_cast<float>(b)};
            if (last_editor_frame_.x != scaled.x ||
                last_editor_frame_.y != scaled.y ||
                last_editor_frame_.width != scaled.width ||
                last_editor_frame_.height != scaled.height) {
                chain_[static_cast<size_t>(attached_index_)]
                    ->SetPluginEditorFrame(l, t, r, b);
                last_editor_frame_ = scaled;
            }
        }
    }

    // ---- CEF pane --------------------------------------------------

    void SyncBrowserFrame() {
        if (torn_down_ || browser_rect_.width <= 1.0f ||
            browser_rect_.height <= 1.0f) {
            return;
        }
        const float s = DpiScale();
        const UI::Layout::Rect rect = browser_rect_.Intersect(GetVisibleRect());
        const int x = static_cast<int>(rect.x * s);
        const int y = static_cast<int>(rect.y * s);
        const int w = static_cast<int>(rect.width * s);
        const int h = static_cast<int>(rect.height * s);
        if (!cef_browser_) {
            if (cef_create_attempted_ || !cef_available_) {
                return;
            }
            cef_create_attempted_ = true;
            CreateBrowserPane(x, y, w, h);
            return;
        }
        const UI::Layout::Rect scaled{static_cast<float>(x),
                                  static_cast<float>(y),
                                  static_cast<float>(x + w),
                                  static_cast<float>(y + h)};
        if (last_browser_frame_.x != scaled.x ||
            last_browser_frame_.y != scaled.y ||
            last_browser_frame_.width != scaled.width ||
            last_browser_frame_.height != scaled.height) {
            cef_.Resize(x, y, w, h);
            last_browser_frame_ = scaled;
        }
    }

    void CreateBrowserPane(int x, int y, int w, int h) {
        XER::Cef::HostEvents events;
        auto pending = cef_pending_;
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
        const std::string url =
            options_.url.empty() ? "about:blank" : options_.url;
        cef_browser_ =
            cef_.Create(parent_hwnd_, x, y, w, h, url, std::move(events));
        if (cef_browser_) {
            if (options_.url.empty()) {
                cef_.LoadHtml(kWelcomeHtml);
            }
            SetStatus("CEF browser ready");
        } else {
            cef_error_ = "browser creation failed";
        }
        Invalidate();
    }

    void Navigate() {
        std::string url = url_input_->GetValue();
        if (url.empty() || !cef_browser_) {
            return;
        }
        if (url.find("://") == std::string::npos && url != "about:blank") {
            url = "https://" + url;
        }
        cef_.LoadUrl(url);
    }

    // Applies queued CEF-thread events on the UI thread.
    void PumpCefEvents() {
        std::optional<std::string> url, title;
        std::optional<bool> loading;
        bool closed = false;
        {
            std::lock_guard<std::mutex> lk(cef_pending_->mutex);
            url = std::exchange(cef_pending_->url, std::nullopt);
            title = std::exchange(cef_pending_->title, std::nullopt);
            loading = std::exchange(cef_pending_->loading, std::nullopt);
            closed = std::exchange(cef_pending_->closed, false);
        }
        if (url) {
            last_url_ = *url;
            url_input_->SetValue(*url);
        }
        if (title && window_ != nullptr) {
            window_->SetTitle(*title + " — XER VST3+CEF Demo");
        }
        if (loading) {
            SetStatus(*loading ? "Loading " + last_url_ + " ..."
                               : "Loaded " + last_url_);
        }
        if (closed) {
            cef_browser_ = false;
            SetStatus("browser closed");
            Invalidate();
        }
    }

    UI::Core::Window* window_ = nullptr;
    void* parent_hwnd_ = nullptr;
    Vst3CefDemoOptions options_;
    std::string cef_error_;
    bool cef_available_ = false;

    // VST3 chain: every successfully loaded plugin stays alive and
    // processes audio in order; chain_active_ picks whose editor the
    // left pane shows. chain_paths_ is the parallel source-path list
    // (GetLoadedPluginId reports the plugin name, not the path).
    std::vector<std::unique_ptr<Audio::Plugin::Vst3HostInterface>> chain_;
    std::vector<std::string> chain_paths_;
    // Parallel per-slot FX bypass (right-click chip to toggle): bypassed
    // plugins are skipped in ProcessAudioBlock so the dry signal passes.
    std::vector<uint8_t> chain_bypass_;
    int chain_active_ = -1;
    int attached_index_ = -1;
    // Guards chain_/chain_paths_ against the audio thread's
    // ProcessAudioBlock iteration — held only for vector mutation or a
    // single chain pass, never across plugin load/unload.
    std::mutex chain_mutex_;

    Cef::CefHost cef_;
    std::shared_ptr<ChainStrip> chain_strip_;

    // Audio file -> chain -> output.
    std::unique_ptr<Content::Audio::AudioPlayer> player_;
    int audio_rate_ = 48000;
    std::atomic<bool> audio_state_dirty_{false};
    std::atomic<float> audio_in_peak_{0.0f};
    std::atomic<float> audio_out_peak_{0.0f};
    int audio_meter_tick_ = 0;
    std::vector<std::vector<float>> audio_chans_;   // audio-thread scratch

    std::shared_ptr<UI::Widgets::TextInput> audio_path_input_;
    std::shared_ptr<UI::Widgets::Button> audio_browse_button_;
    std::shared_ptr<UI::Widgets::Button> audio_play_button_;
    std::shared_ptr<UI::Widgets::Button> audio_stop_button_;
    std::shared_ptr<UI::Widgets::Label> audio_time_;

    std::shared_ptr<UI::Widgets::Select> plugin_select_;
    std::shared_ptr<UI::Widgets::TextInput> plugin_path_input_;
    std::shared_ptr<UI::Widgets::Button> browse_button_;
    std::shared_ptr<UI::Widgets::Button> scan_button_;
    std::shared_ptr<UI::Widgets::Button> load_button_;
    std::shared_ptr<UI::Widgets::Button> unload_button_;
    std::shared_ptr<UI::Widgets::Button> back_button_;
    std::shared_ptr<UI::Widgets::Button> fwd_button_;
    std::shared_ptr<UI::Widgets::Button> reload_button_;
    std::shared_ptr<UI::Widgets::Button> devtools_button_;
    std::shared_ptr<UI::Widgets::TextInput> url_input_;
    std::shared_ptr<UI::Widgets::Button> go_button_;
    std::shared_ptr<UI::Widgets::Label> status_;

    std::vector<Audio::Plugin::Vst3PluginInfo> scanned_plugins_;
    bool editor_attached_ = false;
    bool editor_attach_pending_ = false;
    bool cef_browser_ = false;
    bool cef_create_attempted_ = false;
    bool torn_down_ = false;

    UI::Layout::Rect editor_rect_ {};
    UI::Layout::Rect browser_rect_ {};
    UI::Layout::Rect last_editor_frame_ {};
    UI::Layout::Rect last_browser_frame_ {};

    std::shared_ptr<CefPending> cef_pending_ =
        std::make_shared<CefPending>();
    std::string last_url_;
};

}  // namespace

int RunVst3CefDemo(const Vst3CefDemoOptions& options) {
    auto& app = XER::UI::Core::Application::Instance();
    if (!app.Initialize()) {
        std::fprintf(stderr, "UI application init failed\n");
        return 1;
    }
    app.SetAppName("XER VST3+CEF Demo");

    // Best-effort: failure lands as placeholder text in the CEF pane.
    // CEF echoes ERROR+ to stderr even with a log file — LOGSEVERITY_
    // DISABLE (99) is the only level that keeps the console clean of
    // Chromium's registry/extension chatter; FATAL still reaches it.
    XER::Cef::RuntimeOptions cef_opts;
    cef_opts.log_severity = 99;   // LOGSEVERITY_DISABLE
    if (std::getenv("XER_CEF_DEBUG")) {
        cef_opts.log_severity = 1;   // LOGSEVERITY_VERBOSE
    }
    // The GPU child process traps on this build (0xC0000139 exit loop
    // — see docs/wiki/architecture/live-ui-testing.md); the browser
    // pane does not need hardware acceleration.
    cef_opts.disable_gpu = true;
    // Let pages start media without a user gesture — the demo exists to
    // exercise the browser pane, not to gate playback behind a click.
    cef_opts.command_line_switches = {
        "autoplay-policy=no-user-gesture-required",
    };
    wchar_t temp[MAX_PATH] {};
    if (GetTempPathW(MAX_PATH, temp) != 0) {
        cef_opts.log_file = XER::Helper::String::WideToUtf8(temp) +
                            "xer_vst3_cef_debug.log";
    }
    std::string cef_error;
    const bool cef_ok = XER::Cef::Initialize(cef_opts, &cef_error);
    if (!cef_ok) {
        std::fprintf(stderr, "CEF unavailable: %s\n", cef_error.c_str());
    }

    auto window = std::make_shared<XER::UI::Core::Window>();
    if (!window->Create("XER Demo — VST3 View + CEF View", 1280, 800)) {
        std::fprintf(stderr, "window creation failed\n");
        XER::Cef::Shutdown();
        app.Shutdown();
        return 1;
    }
    window->SetTheme(XER::UI::Theme::Variant::Dark);
    window->SetSizeLimits(720, 480, 0, 0);

    auto demo = std::make_shared<Vst3CefDemoWidget>(window.get(), options,
                                                    cef_error, cef_ok);
    window->SetContent(demo);
    window->SetFrameCallback([demo] { demo->TickTree(0.033); }, 33);
    window->Show();

    const int rc = app.Run();

    // Native children die while CEF is still up: detach + close before
    // shutting the runtimes down.
    demo->Teardown();
    XER::Cef::Shutdown();
    app.Shutdown();
    return rc;
}

}  // namespace XER::Demo

#else   // !_WIN32 — HWND embedding is Windows-only here.

namespace XER::Demo {
int RunVst3CefDemo(const Vst3CefDemoOptions&) {
    std::fputs("XER_VST3_CEF demo is Windows-only\n", stderr);
    return 1;
}
}  // namespace XER::Demo

#endif  // _WIN32

namespace {

void PrintUsage() {
    std::printf(
        "Usage:\n"
        "  XER_VST3_CEF [options]\n"
        "  XER_VST3_CEF <plugin.vst3> [url]\n"
        "\n"
        "Options:\n"
        "  -p, --plugin <path>     .vst3 bundle/module to load at startup\n"
        "  -u, --url <url>         URL for the CEF pane\n"
        "  -a, --audio <path>      audio file routed through the VST3 chain\n"
        "      --sample-rate <hz>  plugin sample rate (default 48000)\n"
        "  -h, --help              show this help\n");
}

#if defined(_WIN32)
// CommandLineToArgvW + WideToUtf8 keeps non-ASCII paths intact (CRT
// argv is ANSI on Windows).
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

}  // namespace

int main(int argc, char** argv) {
#if defined(_WIN32)
    // CEF child processes never return from this — must run before any
    // other initialization. For the browser process it returns -1.
    const int cef_exit = XER::Cef::RunSubprocessIfAny(argc, argv);
    if (cef_exit >= 0) {
        return cef_exit;
    }
    SetConsoleOutputCP(CP_UTF8);
    // Crash reporter: the plugin chain runs third-party code on a live
    // audio thread; an access violation there kills the process silently
    // otherwise. Print fault code/address/module and let the default
    // handler continue.
    AddVectoredExceptionHandler(
        1, [](EXCEPTION_POINTERS* ep) -> LONG {
            const EXCEPTION_RECORD* r = ep->ExceptionRecord;
            if (r->ExceptionCode != 0xC0000005 &&
                r->ExceptionCode != 0xC00000FD) {
                return EXCEPTION_CONTINUE_SEARCH;   // noise: thread names, RPC
            }
            auto print_mod = [](const void* addr) {
                HMODULE mod = nullptr;
                GetModuleHandleExA(
                    GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                    reinterpret_cast<LPCSTR>(addr), &mod);
                char name[MAX_PATH] {};
                if (mod != nullptr) {
                    GetModuleFileNameA(mod, name, MAX_PATH);
                }
                std::fprintf(stderr, "  %p  %s+0x%llx\n", addr, name,
                             mod != nullptr
                                 ? (unsigned long long)(
                                       (const char*)addr - (const char*)mod)
                                 : 0ULL);
            };
            std::fprintf(stderr, "CRASH: code=0x%lx addr=%p\n",
                         r->ExceptionCode, (void*)r->ExceptionAddress);
            print_mod(r->ExceptionAddress);
            // Walk the crashing thread's stack from its context record.
            CONTEXT ctx = *ep->ContextRecord;
            for (int i = 0; i < 24 && ctx.Rip != 0; ++i) {
                print_mod(reinterpret_cast<void*>(ctx.Rip));
                DWORD64 image_base = 0;
                PRUNTIME_FUNCTION fn =
                    RtlLookupFunctionEntry(ctx.Rip, &image_base, nullptr);
                if (fn == nullptr) {   // leaf function
                    ctx.Rip = *reinterpret_cast<DWORD64*>(ctx.Rsp);
                    ctx.Rsp += 8;
                    continue;
                }
                PVOID handler = nullptr;
                DWORD64 establisher = 0;
                RtlVirtualUnwind(UNW_FLAG_NHANDLER, image_base, ctx.Rip,
                                 fn, &ctx, &handler, &establisher,
                                 nullptr);
            }
            return EXCEPTION_CONTINUE_SEARCH;
        });
    const std::vector<std::string> args = CommandLineArgsUtf8();
#else
    const std::vector<std::string> args(argv, argv + argc);
#endif

    XER::Demo::Vst3CefDemoOptions options;
    std::vector<std::string> positional;
    for (size_t i = 1; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (a == "-h" || a == "--help") {
            PrintUsage();
            return 0;
        } else if ((a == "-p" || a == "--plugin") &&
                   i + 1 < args.size()) {
            options.plugin_path = args[++i];
        } else if ((a == "-u" || a == "--url") && i + 1 < args.size()) {
            options.url = args[++i];
        } else if ((a == "-a" || a == "--audio") && i + 1 < args.size()) {
            options.audio_path = args[++i];
        } else if (a == "--sample-rate" && i + 1 < args.size()) {
            options.sample_rate = std::atof(args[++i].c_str());
        } else if (!a.empty() && a[0] == '-') {
            std::fprintf(stderr, "unknown option: %s\n", a.c_str());
            PrintUsage();
            return 1;
        } else {
            positional.push_back(a);
        }
    }
    if (!positional.empty() && options.plugin_path.empty()) {
        options.plugin_path = positional.front();
    }
    if (positional.size() > 1 && options.url.empty()) {
        options.url = positional[1];
    }
    return XER::Demo::RunVst3CefDemo(options);
}
