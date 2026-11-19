// XERVacUI — Virtual Audio Cable control panel on src/ui.
//
// Second UI program next to the VST3+CEF demo: cable management
// (create/enumerate), WASAPI device browsing, route setup through the
// shared VacRoute engine (same endpoints/FX chains as XERVac.exe),
// JSON themes, UI event-bus instrumentation, and an optional remote
// render mode that streams frames to xer_engine_ui_render.exe via a
// Cache::SharedMemoryChannel.

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include <windows.h>

#include "core/render_host.h"
#include "core/application.h"
#include "core/ui_events.h"
#include "core/window.h"
#include "helper/string_utils.h"
#include "json/json.h"
#include "rendering/render_stream.h"
#include "virtualaudiocable/cable_wasapi.h"
#include "virtualaudiocable/vac_engine.h"
#include "virtualaudiocable/virtual_audio_cable.h"
#include "widgets/button.h"
#include "widgets/label.h"
#include "widgets/text_input.h"
#include "widgets/widget_tree.h"

#ifdef _WIN32

namespace {

using namespace XER::UI;
using XER::Audio::Core::VirtualAudioCable;
using XER::Audio::Core::VirtualCableConfig;
using XER::Audio::Core::VirtualCableInfo;
using XER::Audio::Core::WasapiDeviceInfo;
using XER::Audio::Core::VacEndpoint;
using XER::Audio::Core::VacRoute;
using XER::Audio::Core::ParseVacEndpoint;
namespace Stream = XER::UI::Rendering::Stream;

std::atomic<bool> g_quit{false};

// Simple vertical list: rows of text, click selects. Rows are rebuilt
// by SetRows; the selected index is kept stable by value.
class ListBox final : public Widgets::Widget {
public:
    void SetRows(std::vector<std::string> rows) {
        rows_ = std::move(rows);
        if (selected_ >= static_cast<int>(rows_.size())) {
            selected_ = rows_.empty() ? -1 : 0;
        }
        Invalidate();
    }
    int Selected() const { return selected_; }
    std::string SelectedText() const {
        return selected_ >= 0 && selected_ < (int)rows_.size()
                   ? rows_[selected_]
                   : std::string{};
    }
    void SetOnSelect(std::function<void(int)> cb) {
        on_select_ = std::move(cb);
    }

    void Render(Rendering::Renderer& renderer,
                const Theme::Tokens& tokens) override {
        const auto b = GetBounds();
        renderer.FillRect(b, tokens.input, tokens.radius_small);
        renderer.StrokeRect(b, tokens.border, 1.0f, tokens.radius_small);
        const float row_h = 24.0f;
        const int visible = static_cast<int>(b.height / row_h);
        int start = 0;
        if (selected_ >= visible) {
            start = selected_ - visible + 1;
        }
        for (int i = start; i < (int)rows_.size() &&
                              i - start < visible;
             ++i) {
            const Layout::Rect row{b.x + 2, b.y + 2 + (i - start) * row_h,
                                   b.width - 4, row_h - 2};
            if (i == selected_) {
                renderer.FillRect(row, tokens.secondary,
                                  tokens.radius_small);
            }
            Rendering::TextStyle style;
            style.color = i == selected_ ? tokens.foreground
                                         : tokens.muted;
            style.size = tokens.font_small;
            renderer.DrawText(rows_[i],
                              {row.x + 8, row.y, row.width - 16,
                               row.height},
                              style);
        }
        if (rows_.empty()) {
            Rendering::TextStyle style;
            style.color = tokens.disabled;
            style.size = tokens.font_small;
            renderer.DrawText("(empty)",
                              {b.x + 8, b.y + 6, b.width - 16, 20},
                              style);
        }
    }

    void Layout(const Layout::Rect& bounds) override {
        SetBounds(bounds);
    }

    bool HandleEvent(const Widgets::Event& event) override {
        if (event.type != Widgets::Event::Type::MouseDown &&
            event.type != Widgets::Event::Type::Click) {
            return false;
        }
        const auto b = GetBounds();
        if (event.position.x < b.x || event.position.x >= b.x + b.width ||
            event.position.y < b.y ||
            event.position.y >= b.y + b.height) {
            return false;
        }
        const float row_h = 24.0f;
        const int visible = static_cast<int>(b.height / row_h);
        int start = 0;
        if (selected_ >= visible) {
            start = selected_ - visible + 1;
        }
        const int idx =
            start + static_cast<int>((event.position.y - b.y - 2) / row_h);
        if (idx >= 0 && idx < (int)rows_.size()) {
            selected_ = idx;
            Invalidate();
            if (on_select_) on_select_(idx);
        }
        return true;
    }

    Layout::Size GetMinSize() const override { return {120, 60}; }
    Layout::Size GetPreferredSize() const override { return {240, 120}; }
    Layout::Size GetMaxSize() const override {
        return {10000, 10000};
    }

private:
    std::vector<std::string> rows_;
    int selected_ = -1;
    std::function<void(int)> on_select_;
};

class VacUiWidget final : public Widgets::Widget {
public:
    explicit VacUiWidget(Core::Window* window) : window_(window) {
        status_ = std::make_shared<Widgets::Label>("Ready");
        refresh_btn_ =
            std::make_shared<Widgets::Button>("Refresh");
        cables_list_ = std::make_shared<ListBox>();
        devices_list_ = std::make_shared<ListBox>();
        routes_list_ = std::make_shared<ListBox>();

        name_in_ = std::make_shared<Widgets::TextInput>("VAC1");
        rate_in_ = std::make_shared<Widgets::TextInput>("48000");
        create_btn_ = std::make_shared<Widgets::Button>("Create cable");

        src_in_ = std::make_shared<Widgets::TextInput>("cable:VAC1");
        dst_in_ = std::make_shared<Widgets::TextInput>(
            "device:default");
        fx_in_ = std::make_shared<Widgets::TextInput>(
            "gain:-6;softclip");
        start_route_btn_ =
            std::make_shared<Widgets::Button>("Start route");
        stop_route_btn_ =
            std::make_shared<Widgets::Button>("Stop route");

        theme_btn_ = std::make_shared<Widgets::Button>("Theme");
        render_btn_ =
            std::make_shared<Widgets::Button>("Remote render");

        title_ = std::make_shared<Widgets::Label>(
            "XER Virtual Audio Cable");
        stats_ = std::make_shared<Widgets::Label>("");

        for (auto& w : {std::shared_ptr<Widgets::Widget>(title_),
                        std::shared_ptr<Widgets::Widget>(refresh_btn_),
                        std::shared_ptr<Widgets::Widget>(cables_list_),
                        std::shared_ptr<Widgets::Widget>(devices_list_),
                        std::shared_ptr<Widgets::Widget>(routes_list_),
                        std::shared_ptr<Widgets::Widget>(name_in_),
                        std::shared_ptr<Widgets::Widget>(rate_in_),
                        std::shared_ptr<Widgets::Widget>(create_btn_),
                        std::shared_ptr<Widgets::Widget>(src_in_),
                        std::shared_ptr<Widgets::Widget>(dst_in_),
                        std::shared_ptr<Widgets::Widget>(fx_in_),
                        std::shared_ptr<Widgets::Widget>(start_route_btn_),
                        std::shared_ptr<Widgets::Widget>(stop_route_btn_),
                        std::shared_ptr<Widgets::Widget>(theme_btn_),
                        std::shared_ptr<Widgets::Widget>(render_btn_),
                        std::shared_ptr<Widgets::Widget>(status_),
                        std::shared_ptr<Widgets::Widget>(stats_)}) {
            AddChild(w);
        }

        refresh_btn_->SetOnClick([this] { RefreshAll(); });
        create_btn_->SetOnClick([this] { CreateCable(); });
        start_route_btn_->SetOnClick([this] { StartRouteUi(); });
        stop_route_btn_->SetOnClick([this] { StopRouteUi(); });
        theme_btn_->SetOnClick([this] { ToggleTheme(); });
        render_btn_->SetOnClick([this] { ToggleRemoteRender(); });
        devices_list_->SetOnSelect([this](int idx) {
            if (idx >= 0 && idx < (int)devices_.size()) {
                const auto& d = devices_[idx];
                const std::string id =
                    XER::Helper::String::WideToUtf8(d.id);
                dst_in_->SetValue(d.render ? "device:" + id
                                          : "capture:" + id);
            }
        });
        cables_list_->SetOnSelect([this](int idx) {
            if (idx >= 0 && idx < (int)cables_.size()) {
                src_in_->SetValue("cable:" + cables_[idx].name);
                name_in_->SetValue(cables_[idx].name);
            }
        });
        RefreshAll();
    }

    ~VacUiWidget() override { StopRemoteRender(); }

    void Layout(const Layout::Rect& bounds) override {
        SetBounds(bounds);
        const float pad = 12.0f, row = 30.0f, gap = 8.0f;
        float y = bounds.y + pad;
        title_->Layout({bounds.x + pad, y, bounds.width - 2 * pad, 28});
        y += 32;
        const float half = (bounds.width - 2 * pad - gap) / 2.0f;
        cables_list_->Layout(
            {bounds.x + pad, y, half, 130});
        devices_list_->Layout(
            {bounds.x + pad + half + gap, y, half, 130});
        y += 138;
        refresh_btn_->Layout(
            {bounds.x + pad, y, 90, row});
        name_in_->Layout(
            {bounds.x + pad + 100, y, 140, row});
        rate_in_->Layout(
            {bounds.x + pad + 248, y, 90, row});
        create_btn_->Layout(
            {bounds.x + pad + 346, y, 110, row});
        y += row + gap;
        src_in_->Layout({bounds.x + pad, y, half, row});
        dst_in_->Layout(
            {bounds.x + pad + half + gap, y, half, row});
        y += row + gap;
        fx_in_->Layout({bounds.x + pad, y, bounds.width - 2 * pad, row});
        y += row + gap;
        start_route_btn_->Layout({bounds.x + pad, y, 110, row});
        stop_route_btn_->Layout(
            {bounds.x + pad + 120, y, 110, row});
        theme_btn_->Layout(
            {bounds.x + pad + 240, y, 90, row});
        render_btn_->Layout(
            {bounds.x + pad + 340, y, 130, row});
        y += row + gap;
        routes_list_->Layout(
            {bounds.x + pad, y, bounds.width - 2 * pad, 90});
        y += 98;
        stats_->Layout({bounds.x + pad, y, bounds.width - 2 * pad, 20});
        y += 24;
        status_->Layout({bounds.x + pad, y, bounds.width - 2 * pad, 24});
    }

    void Render(Rendering::Renderer& renderer,
                const Theme::Tokens& tokens) override {
        renderer.FillRect(GetBounds(), tokens.background, 0.0f);
    }

    bool HandleEvent(const Widgets::Event& event) override {
        (void)event;
        return false;
    }

    Layout::Size GetMinSize() const override { return {560, 560}; }
    Layout::Size GetPreferredSize() const override { return {820, 620}; }
    Layout::Size GetMaxSize() const override { return {10000, 10000}; }

    void TickUi() {
        stats_->SetText(MakeStatsLine());
    }

private:
    std::string MakeStatsLine() {
        char buf[160];
        std::snprintf(buf, sizeof(buf),
                      "%zu cable host(s) | %zu route(s) | %zu device(s)",
                      owned_cables_.size(), routes_.size(),
                      devices_.size());
        return buf;
    }

    void SetStatus(const std::string& s) {
        status_->SetText(s);
        Invalidate();
    }

    void RefreshAll() {
        devices_ = XER::Audio::Core::EnumerateWasapiDevices();
        std::vector<std::string> dev_rows;
        for (const auto& d : devices_) {
            dev_rows.push_back(std::string(d.render ? "[out] " : "[in]  ") +
                               d.name);
        }
        devices_list_->SetRows(std::move(dev_rows));

        cables_ = VirtualAudioCable::Enumerate();
        std::vector<std::string> cab_rows;
        for (const auto& c : cables_) {
            char row[160];
            std::snprintf(row, sizeof(row),
                          "%s  (%u Hz, %u ch, pid %lu)", c.name.c_str(),
                          c.sample_rate, c.channels,
                          (unsigned long)c.host_pid);
            cab_rows.push_back(row);
        }
        cables_list_->SetRows(std::move(cab_rows));
        SetStatus("refreshed");
    }

    void CreateCable() {
        VirtualCableConfig cfg;
        cfg.name = name_in_->GetValue();
        cfg.sample_rate =
            static_cast<uint32_t>(std::atoi(rate_in_->GetValue().c_str()));
        cfg.channels = 2;
        cfg.buffer_ms = 250;
        if (cfg.name.empty()) {
            SetStatus("cable name required");
            return;
        }
        std::string err;
        auto cable = VirtualAudioCable::Create(cfg, &err);
        if (!cable) {
            SetStatus("create failed: " + err);
            return;
        }
        owned_cables_.push_back(std::move(cable));
        SetStatus("cable '" + cfg.name + "' up");
        RefreshAll();
    }

    // Builds the chain json from "gain:-6;softclip;vst3:path" syntax.
    static Json::Value ParseChainText(const std::string& s) {
        Json::Value chain(Json::arrayValue);
        size_t pos = 0;
        while (pos <= s.size()) {
            const size_t end = s.find(';', pos);
            const std::string item = s.substr(
                pos, end == std::string::npos ? std::string::npos
                                              : end - pos);
            pos = end == std::string::npos ? s.size() + 1 : end + 1;
            if (item.empty()) continue;
            const auto colon = item.find(':');
            const std::string name =
                colon == std::string::npos ? item : item.substr(0, colon);
            Json::Value stage;
            if (name == "vst3") {
                stage["type"] = "vst3";
                stage["path"] = item.substr(colon + 1);
            } else {
                stage["type"] = "fx";
                stage["name"] = name;
                if (colon != std::string::npos) {
                    const std::string val = item.substr(colon + 1);
                    const auto eq = val.find('=');
                    Json::Value params(Json::objectValue);
                    if (eq != std::string::npos) {
                        params[val.substr(0, eq)] =
                            std::atof(val.substr(eq + 1).c_str());
                    } else {
                        params["amount"] = std::atof(val.c_str());
                        if (name == "gain") {
                            params["db"] = params["amount"];
                        }
                    }
                    stage["params"] = params;
                }
            }
            chain.append(stage);
        }
        return chain;
    }

    void StartRouteUi() {
        const auto src = ParseVacEndpoint(src_in_->GetValue());
        const auto dst = ParseVacEndpoint(dst_in_->GetValue());
        if (!src || !dst) {
            SetStatus("bad endpoint syntax (cable:|device:|capture:|"
                      "loopback:|wav:|null)");
            return;
        }
        std::string err;
        auto route =
            VacRoute::Start(*src, *dst,
                            ParseChainText(fx_in_->GetValue()), &err);
        if (!route) {
            SetStatus("route failed: " + err);
            return;
        }
        routes_.push_back(std::move(route));
        RebuildRouteRows();
        SetStatus("route running");
    }

    void StopRouteUi() {
        const int sel = routes_list_->Selected();
        if (sel >= 0 && sel < (int)routes_.size()) {
            routes_.erase(routes_.begin() + sel);
        } else if (!routes_.empty()) {
            routes_.pop_back();
        }
        RebuildRouteRows();
        SetStatus("route stopped");
    }

    void RebuildRouteRows() {
        std::vector<std::string> rows;
        for (size_t i = 0; i < routes_.size(); ++i) {
            char row[80];
            std::snprintf(row, sizeof(row), "route #%zu (running)",
                          i + 1);
            rows.push_back(row);
        }
        routes_list_->SetRows(std::move(rows));
    }

    void ToggleTheme() {
        dark_ = !dark_;
        window_->SetTheme(dark_ ? Theme::Variant::Dark
                                : Theme::Variant::Light);
        Invalidate();
    }

    // ---- remote render ----------------------------------------------
    void ToggleRemoteRender() {
        if (remote_renderer_) {
            remote_renderer_.reset();
            window_->SetRenderer(nullptr);
            SetStatus("local render");
            return;
        }
        std::string err;
        // One hidden xer_engine_ui_render.exe process draws straight
        // into this window's HWND via the shared-memory channel.
        remote_renderer_ =
            Core::UiRenderHost::Instance().CreateRemoteRenderer(
                reinterpret_cast<uintptr_t>(window_->GetHWND()), &err);
        if (!remote_renderer_) {
            SetStatus("remote render: " + err);
            return;
        }
        window_->SetRenderer(std::move(remote_renderer_));
        window_->Invalidate();
        SetStatus("remote render via xer_engine_ui_render.exe");
    }

    void StopRemoteRender() {
        remote_renderer_.reset();
        Core::UiRenderHost::Instance().ShutdownAll();
    }

    Core::Window* window_;
    std::shared_ptr<Widgets::Label> title_, status_, stats_;
    std::shared_ptr<Widgets::Button> refresh_btn_, create_btn_,
        start_route_btn_, stop_route_btn_, theme_btn_, render_btn_;
    std::shared_ptr<ListBox> cables_list_, devices_list_, routes_list_;
    std::shared_ptr<Widgets::TextInput> name_in_, rate_in_, src_in_,
        dst_in_, fx_in_;

    std::vector<VirtualCableInfo> cables_;
    std::vector<WasapiDeviceInfo> devices_;
    std::vector<std::unique_ptr<VirtualAudioCable>> owned_cables_;
    std::vector<std::unique_ptr<VacRoute>> routes_;

    bool dark_ = true;
    std::unique_ptr<Rendering::StreamRenderer> remote_renderer_;
};

}  // namespace

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    auto& app = Core::Application::Instance();
    if (!app.Initialize()) {
        std::fprintf(stderr, "UI application init failed\n");
        return 1;
    }
    app.SetAppName("XER VAC UI");
    XER::Audio::Core::SetVacServiceQuitFlag(&g_quit);

    auto window = std::make_shared<Core::Window>();
    if (!window->Create("XER Virtual Audio Cable", 860, 660)) {
        app.Shutdown();
        return 1;
    }
    window->SetTheme(Theme::Variant::Dark);
    window->SetSizeLimits(600, 560, 0, 0);

    // Event system: count events + observe frames — demonstrates the
    // UiEventBus/UiHooks infrastructure on a real app.
    auto bus = std::make_shared<Core::UiEventBus>();
    static std::atomic<uint64_t> event_count{0}, frame_count{0};
    bus->SubscribeAll([](const Widgets::Event&) { ++event_count; });
    bus->AddRenderHook(Core::UiEventBus::RenderPhase::kAfterFrame,
                       [](const Core::UiRenderFrame&) {
                           ++frame_count;
                       });
    window->SetEventBus(bus);

    auto panel = std::make_shared<VacUiWidget>(window.get());
    window->SetContent(panel);
    window->SetFrameCallback([panel] { panel->TickUi(); }, 1000);
    window->Show();

    const int rc = app.Run();
    app.Shutdown();
    return rc;
}

#else

int main() {
    std::fprintf(stderr, "XERVacUI is Windows-only\n");
    return 1;
}

#endif
