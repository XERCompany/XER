#include "settings_widget.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

#include "async_io/hardware_abstraction/system_info.h"
#include "cache/cache_manager.h"
#include "rendering/renderer.h"
#include "theme/tokens.h"
#include "widgets/button.h"
#include "widgets/label.h"
#include "widgets/select.h"
#include "widgets/switch.h"

namespace XER::UI::Widgets {

// Pimpl wrapper: keeps hardware_abstraction types out of the public header.
struct SettingsWidget::SysProvider {
    XER::AsyncIO::IO::Hardware::SystemInfoProvider provider;
    std::future<XER::AsyncIO::IO::Hardware::SystemSnapshot> future;
    bool pending = false;
};

namespace {

constexpr float kPadX = 18.0f;
constexpr float kSideGap = 14.0f;   // keep clear of the scrollbar strip
constexpr float kTopPad = 10.0f;
constexpr float kBottomPad = 18.0f;
constexpr float kCardPad = 14.0f;
constexpr float kSectionGap = 16.0f;
constexpr float kLabelW = 150.0f;
constexpr float kControlGap = 10.0f;
constexpr float kInfoH = 24.0f;
// Row heights (DIP).
constexpr float kSwitchRowH = 42.0f;
constexpr float kDescRowH = 48.0f;
constexpr float kButtonRowH = 46.0f;
constexpr float kHeaderGap = 8.0f;

std::string ThemeName(Theme::Variant v) {
    return v == Theme::Variant::Dark ? "dark" : "light";
}

std::string BytesText(std::uint64_t bytes) {
    constexpr std::uint64_t kb = 1024, mb = kb * 1024, gb = mb * 1024;
    char buf[64];
    if (bytes >= gb) {
        std::snprintf(buf, sizeof(buf), "%.2f GB", static_cast<double>(bytes) / gb);
    } else if (bytes >= mb) {
        std::snprintf(buf, sizeof(buf), "%.1f MB", static_cast<double>(bytes) / mb);
    } else if (bytes >= kb) {
        std::snprintf(buf, sizeof(buf), "%.1f KB", static_cast<double>(bytes) / kb);
    } else {
        std::snprintf(buf, sizeof(buf), "%llu B",
                      static_cast<unsigned long long>(bytes));
    }
    return buf;
}

} // namespace

SettingsWidget::SettingsWidget(void* parent_hwnd, Hooks hooks)
    : parent_hwnd_(parent_hwnd)
    , hooks_(std::move(hooks))
    , sys_provider_(std::make_unique<SysProvider>()) {}

SettingsWidget::~SettingsWidget() = default;

// ── Persistence ────────────────────────────────────────────────────────────

void SettingsWidget::Persist(const std::string& key, const std::string& value) {
    std::string err;
    XER::Cache::CacheManager::Instance().WritePersistentText(
        "settings", "app", key, value, &err);
}

std::string SettingsWidget::LoadSetting(const std::string& key) const {
    std::string value, err;
    if (XER::Cache::CacheManager::Instance().ReadPersistentText(
            "settings", "app", key, &value, &err)) {
        return value;
    }
    return {};
}

// ── Build ──────────────────────────────────────────────────────────────────

void SettingsWidget::BeginSection(const std::string& title,
                                  const std::string& desc) {
    Section s;
    s.title = title;
    s.desc = desc;
    s.first = entries_.size();
    sections_.push_back(std::move(s));
}

void SettingsWidget::AddRow(std::shared_ptr<Widget> label,
                            std::vector<std::shared_ptr<Widget>> controls,
                            float height) {
    Entry e;
    e.label = std::move(label);
    e.controls = std::move(controls);
    e.height = height;
    // Children must join the widget tree or they get bounds but are never
    // rendered or hit-tested — without this the whole settings page is blank.
    if (e.label) {
        AddChild(e.label);
    }
    for (auto& c : e.controls) {
        AddChild(c);
    }
    entries_.push_back(std::move(e));
}

void SettingsWidget::AddDescribedRow(const std::string& title,
                                     const std::string& desc,
                                     std::vector<std::shared_ptr<Widget>> controls,
                                     float height) {
    auto label = std::make_shared<Label>(title, false);
    auto sub = std::make_shared<Label>(desc, true);
    Entry e;
    e.label = label;
    e.desc = sub;
    e.controls = std::move(controls);
    e.height = height;
    AddChild(label);
    AddChild(sub);
    for (auto& c : e.controls) {
        AddChild(c);
    }
    entries_.push_back(std::move(e));
}

void SettingsWidget::AddInfo(const std::string& text, bool muted) {
    auto l = std::make_shared<Label>(text, muted);
    AddRow(nullptr, {l}, kInfoH);
}

void SettingsWidget::Build() {
    built_ = true;

    // ── Appearance ──────────────────────────────────────────────────────
    BeginSection("Appearance", "Interface theme and colors.");
    {
        auto sel = std::make_shared<Select>(
            std::vector<std::string>{"Dark", "Light"}, 0);
        const std::string saved = LoadSetting("theme");
        sel->SetSelectedIndex(saved == "light" ? 1 : 0);
        sel->SetOnChanged([this](int idx) {
            const auto v = idx == 1 ? Theme::Variant::Light
                                    : Theme::Variant::Dark;
            Persist("theme", ThemeName(v));
            if (hooks_.on_theme_changed) {
                hooks_.on_theme_changed(v);
            }
        });
        AddRow(std::make_shared<Label>("Theme"), {sel}, kSwitchRowH);
        AddInfo("Applies instantly and is remembered between sessions.", true);
    }

    // ── Graph ───────────────────────────────────────────────────────────
    BeginSection("Node Graph", "Autosave and project files.");
    {
        const bool autosave = hooks_.autosave_get ? hooks_.autosave_get() : true;
        auto sw = std::make_shared<Switch>(autosave);
        sw->SetOnChanged([this](bool v) {
            if (hooks_.autosave_set) {
                hooks_.autosave_set(v);
            }
        });
        AddDescribedRow("Autosave",
                        "Write the graph to the project database automatically.",
                        {sw}, kDescRowH);

        auto file_label = std::make_shared<Label>("File: —", true);
        graph_file_label_ = file_label.get();
        AddRow(nullptr, {file_label}, kInfoH);

        std::vector<std::shared_ptr<XER::UI::Widgets::Widget>> buttons;
        if (hooks_.on_new_graph) {
            auto b = std::make_shared<Button>("New");
            b->SetOnClick([this] { if (hooks_.on_new_graph) hooks_.on_new_graph(); });
            buttons.push_back(b);
        }
        if (hooks_.on_save_graph) {
            auto b = std::make_shared<Button>("Save…");
            b->SetOnClick([this] { if (hooks_.on_save_graph) hooks_.on_save_graph(); });
            buttons.push_back(b);
        }
        if (hooks_.on_load_graph) {
            auto b = std::make_shared<Button>("Load…");
            b->SetOnClick([this] { if (hooks_.on_load_graph) hooks_.on_load_graph(); });
            buttons.push_back(b);
        }
        if (!buttons.empty()) {
            AddRow(nullptr, buttons, kButtonRowH);
        }
    }

    // ── Audio cache ─────────────────────────────────────────────────────
    BeginSection("Audio Cache", "Decoded audio and temporary data.");
    {
        auto path_label = std::make_shared<Label>("Location: —", true);
        cache_path_label_ = path_label.get();
        AddRow(nullptr, {path_label}, kInfoH);
        auto size_label = std::make_shared<Label>("Size: —", true);
        cache_size_label_ = size_label.get();
        AddRow(nullptr, {size_label}, kInfoH);
        auto status_label = std::make_shared<Label>("Status: —", true);
        cache_status_label_ = status_label.get();
        AddRow(nullptr, {status_label}, kInfoH);

        auto open = std::make_shared<Button>("Open Folder");
        open->SetOnClick([this] { OpenCacheFolder(); });
        auto clear = std::make_shared<Button>("Clear Cache");
        clear->SetOnClick([this] { ClearCache(); });
        AddRow(nullptr, {open, clear}, kButtonRowH);
    }

    // ── Shortcuts ───────────────────────────────────────────────────────
    BeginSection("Keyboard Shortcuts", "Canvas and editor shortcuts.");
    {
        AddInfo("Del — delete selection          Ctrl+A — select all", false);
        AddInfo("Ctrl+Z / Ctrl+Shift+Z — undo / redo", false);
        AddInfo("Ctrl+S / Ctrl+O — save / load    Ctrl+R — run graph", false);
        AddInfo("Arrows / Shift+Arrows — nudge nodes", false);
        AddInfo("Double-click — add node / rename", false);
        AddInfo("Right-click — context menu", false);
        AddInfo("Scroll — zoom      Middle-drag / Ctrl-drag — pan", false);
    }

    // ── System ──────────────────────────────────────────────────────────
    BeginSection("System", "Hardware snapshot and runtime info.");
    {
        AddInfo("Application: XER Node Editor", false);
        AddInfo("UI: Win32 · Direct2D widgets · Skia canvas", false);
        AddInfo("Cache: persistent + temporary storage", false);
        AddInfo("Graph store: SQLite (src/sql) · JSON interchange", false);

        // Live hardware snapshot — populated asynchronously.
        const char* sys_titles[] = {
            "CPU", "Memory", "GPU", "Temperature",
            "Disks", "Network", "Power", "Battery",
        };
        for (const char* t : sys_titles) {
            auto lbl = std::make_shared<Label>(std::string(t) + ": —", true);
            sys_labels_.push_back(lbl.get());
            AddRow(nullptr, {lbl}, kInfoH);
        }
        auto refresh = std::make_shared<Button>("Refresh System Info");
        refresh->SetOnClick([this] { RefreshSystemInfo(); });
        AddRow(nullptr, {refresh}, kButtonRowH);
        RefreshSystemInfo();
    }

    // Close each section's entry range.
    for (size_t i = 0; i < sections_.size(); ++i) {
        sections_[i].last = (i + 1 < sections_.size())
            ? sections_[i + 1].first
            : entries_.size();
    }
    children_attached_ = true;
}

// ── Layout / render ────────────────────────────────────────────────────────

void SettingsWidget::Layout(const Layout::Rect& bounds) {
    Layout(bounds, last_tokens_);
}

void SettingsWidget::Layout(const Layout::Rect& bounds, const Theme::Tokens& tokens) {
    last_tokens_ = tokens;
    if (!built_) {
        Build();
    }
    SetBounds(bounds);

    const float content_w = std::max(80.0f, bounds.width - kPadX * 2.0f - kSideGap);
    const float card_x = bounds.x + kPadX;
    const float card_w = content_w;
    const float inner_x = card_x + kCardPad;
    const float inner_w = card_w - 2.0f * kCardPad;

    // Content starts at the top — the nav tabs already name the panel.
    float y = bounds.y + kTopPad;

    // ── Sections → cards ─────────────────────────────────────────────────
    for (auto& s : sections_) {
        if (s.last <= s.first) continue;
        const float card_top = y;

        // Header block inside the card.
        float cy = card_top + kCardPad;
        s.title_rect = Layout::Rect{inner_x, cy, inner_w, 17.0f};
        cy += 17.0f;
        if (!s.desc.empty()) {
            s.desc_rect = Layout::Rect{inner_x, cy + 1.0f, inner_w, 13.0f};
            cy += 15.0f;
        } else {
            s.desc_rect = Layout::Rect{};
        }
        cy += kHeaderGap;

        // Rows.
        float ry = cy;
        for (std::size_t i = s.first; i < s.last; ++i) {
            auto& e = entries_[i];
            e.top = ry;
            e.bottom = ry + e.height;

            const bool has_label_col = e.label != nullptr || e.desc != nullptr;
            if (e.label) {
                if (e.desc) {
                    e.label->Layout(Layout::Rect{inner_x, ry + 7.0f,
                                                 kLabelW, 15.0f});
                    e.desc->Layout(Layout::Rect{inner_x, ry + 24.0f,
                                                kLabelW, 13.0f});
                } else {
                    e.label->Layout(Layout::Rect{inner_x, ry, kLabelW,
                                                 e.height});
                }
            }

            // Controls start after the label column; each takes its
            // preferred width (Switch stays a small pill instead of
            // stretching across the card), info Labels fill the row.
            float cx = has_label_col ? inner_x + kLabelW + kControlGap : inner_x;
            const float avail = std::max(0.0f, inner_x + inner_w - cx);
            float used = 0.0f;
            const std::size_t n = e.controls.size();
            for (std::size_t ci = 0; ci < n; ++ci) {
                auto& c = e.controls[ci];
                const float remaining = avail - used -
                    (n - ci - 1) * kControlGap;
                float cw, ch;
                const bool is_info_label =
                    dynamic_cast<Label*>(c.get()) != nullptr;
                if (is_info_label) {
                    cw = std::max(remaining, 0.0f);
                    ch = std::max(0.0f, e.height - 4.0f);
                } else {
                    const auto pref = c->GetPreferredSize();
                    cw = std::clamp(pref.width, 36.0f,
                                    std::max(remaining, 36.0f));
                    ch = std::min(pref.height,
                                  std::max(0.0f, e.height - 4.0f));
                }
                c->Layout(Layout::Rect{cx, ry + (e.height - ch) * 0.5f,
                                       cw, ch});
                used += cw + kControlGap;
                cx += cw + kControlGap;
            }
            ry += e.height;
        }

        const float card_bottom = ry + kCardPad;
        s.card = Layout::Rect{card_x, card_top, card_w,
                              card_bottom - card_top};
        s.inner_x = inner_x;
        s.inner_w = inner_w;
        y = card_bottom + kSectionGap;
    }

    content_height_ = y - bounds.y - kSectionGap + kBottomPad;
}

void SettingsWidget::Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) {
    for (const auto& s : sections_) {
        const auto& card = s.card;
        if (card.width <= 0.0f || card.height <= 0.0f) continue;
        renderer.FillRect(card, tokens.surface, tokens.radius_large);
        renderer.StrokeRect(card, tokens.border, 1.0f, tokens.radius_large);

        // Section title + description.
        const Rendering::TextStyle title{tokens.foreground,
                                         tokens.font_body, 600};
        renderer.DrawText(s.title, s.title_rect, title);
        if (s.desc_rect.width > 0.0f) {
            const Rendering::TextStyle desc{tokens.muted,
                                            tokens.font_small, 400};
            renderer.DrawText(s.desc, s.desc_rect, desc);
        }

        // Hairline separators between rows inside the card.
        if (s.last > s.first + 1) {
            const float lx = s.inner_x;
            const float rx = s.inner_x + s.inner_w;
            Theme::Color line = tokens.border;
            line.a *= 0.6f;
            for (std::size_t i = s.first; i + 1 < s.last; ++i) {
                const float yy = entries_[i].bottom;
                renderer.DrawLine(Layout::Point{lx, yy},
                                  Layout::Point{rx, yy}, line, 1.0f);
            }
        }
    }
}

// ── Dynamic info / cache actions ───────────────────────────────────────────

void SettingsWidget::RefreshDynamic() {
    RefreshCacheInfo();
    // Pick up a finished async hardware scan, if any.
    if (sys_provider_ && sys_provider_->pending &&
        sys_provider_->future.wait_for(std::chrono::seconds(0)) ==
            std::future_status::ready) {
        sys_provider_->pending = false;
        try {
            sys_provider_->future.get();  // drains the result; last_ is set
            ApplySystemSnapshot();
        } catch (...) {
            for (auto* l : sys_labels_) l->SetText("—");
        }
        Invalidate();
    }
    if (graph_file_label_ && hooks_.graph_file_get) {
        const std::string path = hooks_.graph_file_get();
        graph_file_label_->SetText(path.empty() ? "File: (unsaved)"
                                                : "File: " + path);
    }
    Invalidate();
}

void SettingsWidget::RefreshCacheInfo() {
    namespace fs = std::filesystem;
    auto& cache = XER::Cache::CacheManager::Instance();
    const auto root = cache.Persistent().root();
    if (cache_path_label_) {
        cache_path_label_->SetText("Location: " + root.string());
    }
    std::uint64_t bytes = 0;
    std::uint64_t files = 0;
    std::error_code ec;
    if (fs::exists(root, ec)) {
        for (auto it = fs::recursive_directory_iterator(
                 root, fs::directory_options::skip_permission_denied, ec);
             !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
            std::error_code fec;
            if (it->is_regular_file(fec)) {
                bytes += it->file_size(fec);
                ++files;
            }
        }
    }
    if (cache_size_label_) {
        cache_size_label_->SetText("Size: " + BytesText(bytes) + "  ·  " +
                                   std::to_string(files) + " files");
    }
}

void SettingsWidget::RefreshSystemInfo() {
    if (!sys_provider_ || sys_provider_->pending) return;
    sys_provider_->future = sys_provider_->provider.RefreshAsync();
    sys_provider_->pending = true;
    if (!sys_labels_.empty()) {
        sys_labels_[0]->SetText("CPU: scanning…");
    }
}

void SettingsWidget::ApplySystemSnapshot() {
    const auto& s = sys_provider_->provider.GetLastSnapshot();
    if (sys_labels_.size() < 8) return;
    namespace H = XER::AsyncIO::IO::Hardware;

    // CPU
    {
        std::string t = s.cpu_description.empty() ? s.cpu.model : s.cpu_description;
        if (t.empty()) t = s.cpu.vendor + " " + s.cpu.model;
        char buf[256];
        std::snprintf(buf, sizeof(buf),
                      "%s · %dC/%dT · %.0f%%",
                      t.empty() ? "unknown" : t.c_str(),
                      s.physical_cores, s.logical_cores, s.cpu_usage_percent);
        sys_labels_[0]->SetText(std::string("CPU: ") + buf);
    }
    // Memory
    {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "%.1f / %.1f GB (%.0f%%)",
                      s.memory.used_mb / 1024.0, s.memory.total_mb / 1024.0,
                      s.memory.usage_percent);
        sys_labels_[1]->SetText(std::string("Memory: ") + buf);
    }
    // GPU
    if (!s.gpus.empty()) {
        std::string t = s.gpus.front().name;
        if (s.gpus.front().total_memory_mb > 0) {
            t += " · " + BytesText(s.gpus.front().total_memory_mb * 1024ull) + " VRAM";
        }
        if (s.gpus.front().util_supported) {
            char b[32];
            std::snprintf(b, sizeof(b), " · %.0f%%", s.gpus.front().gpu_util_percent);
            t += b;
        }
        if (s.gpus.size() > 1) t += " (" + std::to_string(s.gpus.size()) + " GPUs)";
        sys_labels_[2]->SetText("GPU: " + t);
    } else {
        sys_labels_[2]->SetText("GPU: none detected");
    }
    // Temperature
    {
        char buf[96];
        std::snprintf(buf, sizeof(buf), "CPU %.1f°C · max %.1f°C",
                      s.cpu_temperature_c, s.max_temperature_c);
        sys_labels_[3]->SetText(std::string("Temperature: ") + buf);
    }
    // Disks (up to 4, "C: 120 GB free")
    {
        std::string t;
        for (size_t i = 0; i < s.disks.size() && i < 4; ++i) {
            const auto& d = s.disks[i];
            if (!t.empty()) t += "  ";
            t += d.device + ": " + BytesText(d.free_bytes) + " free";
        }
        sys_labels_[4]->SetText("Disks: " + (t.empty() ? std::string("none") : t));
    }
    // Network
    {
        std::string t;
        for (const auto& n : s.network) {
            if (n.is_virtual || n.is_loopback || !n.is_up) continue;
            if (!t.empty()) t += "  ";
            t += n.friendly_name.empty() ? n.name : n.friendly_name;
            if (!n.ipv4_address.empty()) t += " (" + n.ipv4_address + ")";
            if (t.size() > 90) break;
        }
        sys_labels_[5]->SetText("Network: " + (t.empty() ? std::string("no active adapters") : t));
    }
    // Power
    {
        std::string t;
        if (s.power.gpu_power_supported && s.power.gpu_power_watts > 0) {
            char b[48];
            std::snprintf(b, sizeof(b), "GPU %.1f W", s.power.gpu_power_watts);
            t = b;
        }
        if (s.power.cpu_power_supported && s.power.cpu_power_watts > 0) {
            char b[48];
            std::snprintf(b, sizeof(b), "%sCPU %.1f W",
                          t.empty() ? "" : " · ", s.power.cpu_power_watts);
            t += b;
        }
        sys_labels_[6]->SetText("Power: " + (t.empty() ? std::string("n/a") : t));
    }
    // Battery
    if (!s.batteries.empty() && s.batteries.front().present) {
        const auto& b = s.batteries.front();
        char buf[96];
        std::snprintf(buf, sizeof(buf), "%.0f%% · %s", b.percent,
                      b.state_name.empty() ? "unknown" : b.state_name.c_str());
        sys_labels_[7]->SetText(std::string("Battery: ") + buf);
    } else {
        sys_labels_[7]->SetText("Battery: none / desktop");
    }
}

void SettingsWidget::OpenCacheFolder() {
#ifdef _WIN32
    auto& cache = XER::Cache::CacheManager::Instance();
    std::string err;
    cache.Persistent().EnsureRootDirectory(&err);
    const auto root = cache.Persistent().root().wstring();
    ShellExecuteW(nullptr, L"explore", root.c_str(), nullptr, nullptr,
                  SW_SHOWNORMAL);
#endif
}

void SettingsWidget::ClearCache() {
    namespace fs = std::filesystem;
    auto& cache = XER::Cache::CacheManager::Instance();
    const auto root = cache.Persistent().root();
    std::uint64_t removed = 0;
    std::error_code ec;
    if (fs::exists(root, ec)) {
        for (auto it = fs::directory_iterator(root, ec); !ec && it != fs::directory_iterator();
             it.increment(ec)) {
            if (!it->is_directory(ec)) continue;
            const std::string name = it->path().filename().string();
            // Keep UI-owned scopes: autosave, settings and fx state live there.
            if (name == "ui" || name == "settings") continue;
            fs::remove_all(it->path(), ec);
            ++removed;
        }
    }
    if (cache_status_label_) {
        cache_status_label_->SetText(removed > 0
            ? "Cleared " + std::to_string(removed) + " cache scope(s)"
            : "Nothing to clear");
    }
    RefreshCacheInfo();
    Invalidate();
}

} // namespace XER::UI::Widgets
