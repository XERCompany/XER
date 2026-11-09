#include "node_graph_editor_widget.h"
#include "helper/string_utils.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commdlg.h>
#endif

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <numeric>
#include <sstream>
#include <system_error>

#include "cache/cache_manager.h"
#include "content/clipboard/clipboard.h"
#include "node_graph/node_graph.h"
#include "node_graph/node_graph_serializer.h"
#include "rendering/renderer.h"
#include "theme/tokens.h"
#include "widgets/button.h"
#include "widgets/context_menu.h"
#include "widgets/label.h"
#include "widgets/node_param_host.h"
#include "widgets/projects_widget.h"
#include "widgets/scroll_area.h"
#include "widgets/select.h"
#include "widgets/settings_widget.h"
#include "widgets/switch.h"
#include "widgets/tabs.h"
#include "widgets/text_input.h"
#include "widgets/video_preview_widget.h"
#include "widget_tree.h"

namespace XER::UI::Widgets {
namespace NG = XER::NodeGraph;

namespace {

Canvas::Canvas::Color CC(float r, float g, float b, float a = 1.0f) {
    return {r, g, b, a};
}

// Rough advance width for the canvas font (~0.52em average).
float EstTextW(const std::string& s, float px) {
    return static_cast<float>(s.size()) * px * 0.52f;
}

// Palette mirroring the shadcn dark theme.
Canvas::Canvas::Color PortColor(NG::ValueType t) {
    switch (t) {
    case NG::ValueType::kNumber: return CC(0.38f, 0.65f, 0.98f);  // blue-400
    case NG::ValueType::kBool:   return CC(0.98f, 0.57f, 0.24f);  // orange-400
    case NG::ValueType::kString: return CC(0.29f, 0.87f, 0.50f);  // green-400
    case NG::ValueType::kArray:  return CC(0.65f, 0.55f, 0.98f);  // violet-400
    default:                     return CC(0.63f, 0.63f, 0.67f);  // zinc-400
    }
}

Canvas::Canvas::Color MixC(Canvas::Canvas::Color a, Canvas::Canvas::Color b,
                           float t) {
    a.r += (b.r - a.r) * t; a.g += (b.g - a.g) * t;
    a.b += (b.b - a.b) * t; a.a += (b.a - a.a) * t;
    return a;
}

// Studio category accents for node headers — instant visual grouping.
Canvas::Canvas::Color CategoryColor(const std::string& category,
                                    const std::string& type) {
    const std::string key =
        XER::Helper::String::ToLowerAscii(category + " " + type);
    auto has = [&key](std::string_view needle) {
        return key.find(needle) != std::string::npos;
    };
    if (has("math") || has("number") || has("logic") || has("compare"))
        return CC(0.38f, 0.65f, 0.98f);   // blue
    if (has("string") || has("text") || has("regex"))
        return CC(0.29f, 0.87f, 0.50f);   // green
    if (has("array") || has("list") || has("collection") || has("json"))
        return CC(0.65f, 0.55f, 0.98f);   // violet
    if (has("net") || has("http") || has("s3") || has("websocket") ||
        has("network") || has("socket"))
        return CC(0.30f, 0.85f, 0.85f);   // cyan
    if (has("audio") || has("plugin") || has("vst") || has("clap") ||
        has("effect") || has("dsp") || has("lua"))
        return CC(0.98f, 0.60f, 0.24f);   // orange
    if (has("script") || has("python"))
        return CC(0.98f, 0.82f, 0.30f);   // yellow
    if (has("graph") || has("subgraph") || has("flow"))
        return CC(0.51f, 0.55f, 0.97f);   // indigo accent
    if (has("video") || has("image") || has("ffmpeg") || has("media") ||
        has("file") || has("io."))
        return CC(0.95f, 0.45f, 0.65f);   // pink
    return CC(0.63f, 0.66f, 0.72f);       // slate
}

std::string FormatValue(const NG::Value& v) {
    if (v.IsNone()) return "—";
    switch (v.Type()) {
    case NG::ValueType::kNumber: {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.4g", v.AsNumber());
        return buf;
    }
    case NG::ValueType::kBool: return v.AsBool() ? "true" : "false";
    case NG::ValueType::kArray: {
        char buf[24];
        std::snprintf(buf, sizeof(buf), "[%zu]", v.AsArray().size());
        return buf;
    }
    default: {
        std::string s = v.AsString();
        if (s.size() > 18) s = s.substr(0, 17) + "…";
        return s;
    }
    }
}

void AppendUtf8(std::string& out, std::uint32_t cp) {
    if (cp <= 0x7F) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

// Undoable node rename.
class RenameNodeCommand : public NG::Command {
public:
    RenameNodeCommand(NG::NodeId id, std::string title)
        : id_(id), title_(std::move(title)) {}
    bool Apply(NG::Graph& g) override {
        const NG::Node* n = g.FindNode(id_);
        if (n == nullptr) return false;
        if (!captured_) {
            old_ = n->title;
            captured_ = true;
        }
        return g.SetTitle(id_, title_);
    }
    bool Revert(NG::Graph& g) override { return g.SetTitle(id_, old_); }
    std::string Label() const override { return "Rename node"; }
    bool MergeWith(const NG::Command& other) override {
        const auto* c = dynamic_cast<const RenameNodeCommand*>(&other);
        if (c == nullptr || c->id_ != id_) return false;
        title_ = c->title_;
        return true;
    }
private:
    NG::NodeId id_;
    std::string title_, old_;
    bool captured_ = false;
};

#ifdef _WIN32
std::string Utf8FromWide(const std::wstring& w) {
    return XER::Helper::String::WideToUtf8(w);
}

std::wstring OpenFileDialog(HWND owner, const wchar_t* filter, const wchar_t* default_ext,
                            const wchar_t* title, bool save) {
    wchar_t file[MAX_PATH] = L"";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = default_ext;
    ofn.lpstrTitle = title;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (save) {
        ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (!GetSaveFileNameW(&ofn)) return {};
    } else {
        if (!GetOpenFileNameW(&ofn)) return {};
    }
    return std::wstring(file);
}
#endif

constexpr double kNodeW = 190.0;
constexpr double kHeaderH = 26.0;
constexpr double kRowH = 17.0;
constexpr double kParamRowH = 16.0;
constexpr int kMaxParamRows = 6;
constexpr double kPasteStep = 28.0;
// World-space grid snap for node placement/drag — half the base grid
// step, so nodes align tidily without feeling locked to coarse cells.
constexpr double kGridSnap = 16.0;
double SnapWorld(double v) { return std::round(v / kGridSnap) * kGridSnap; }

// Undoable paste: inserts a deserialized node/link set with fresh ids.
// Node subgraphs are deep-copied at construction so pasted subgraph nodes
// never share state with the clipboard source.
class PasteGraphCommand final : public NG::Command {
public:
    PasteGraphCommand(std::vector<NG::Node> nodes, std::vector<NG::Link> links,
                      double dx, double dy)
        : nodes_(std::move(nodes)), links_(std::move(links)),
          dx_(dx), dy_(dy) {}

    bool Apply(NG::Graph& g) override {
        id_map_.clear();
        applied_.clear();
        for (const auto& src : nodes_) {
            NG::Node n = src;  // copy: params/subgraph already deep-copied
            n.x += dx_;
            n.y += dy_;
            n.id = g.next_node_id();
            std::string err;
            if (!g.InsertNode(n, &err)) return false;
            id_map_[src.id] = n.id;
            applied_.push_back(n.id);
        }
        for (const auto& l : links_) {
            const auto fi = id_map_.find(l.from.node);
            const auto ti = id_map_.find(l.to.node);
            if (fi == id_map_.end() || ti == id_map_.end()) continue;
            NG::PortRef from{fi->second, l.from.port};
            NG::PortRef to{ti->second, l.to.port};
            std::string err;
            // Connect assigns a fresh link id and replaces any existing
            // link on the target input — same semantics as canvas linking.
            g.Connect(from, to, &err);
        }
        return true;
    }

    bool Revert(NG::Graph& g) override {
        for (auto it = applied_.rbegin(); it != applied_.rend(); ++it) {
            g.RemoveNode(*it);  // also drops the node's links
        }
        return true;
    }

    std::string Label() const override {
        return "Paste " + std::to_string(nodes_.size()) + " node(s)";
    }

private:
    std::vector<NG::Node> nodes_;
    std::vector<NG::Link> links_;
    double dx_ = 0.0, dy_ = 0.0;
    std::map<NG::NodeId, NG::NodeId> id_map_;
    std::vector<NG::NodeId> applied_;
};

} // namespace

NodeGraphEditorWidget::NodeGraphEditorWidget(
    void* parent_hwnd,
    std::function<void(Theme::Variant)> on_theme_changed,
    std::int64_t initial_project_id)
    : parent_hwnd_(parent_hwnd) {
    auto root = std::make_unique<NG::Graph>();
    NG::Graph* g = root.get();
    Level lvl;
    lvl.owned = std::move(root);
    lvl.graph = g;
    lvl.history = std::make_unique<NG::History>(*g);
    lvl.executor = std::make_unique<NG::Executor>(*g);
    lvl.crumb = "root";
    levels_.push_back(std::move(lvl));

    canvas_.Create(parent_hwnd_);
    canvas_.SetVisible(false);
    canvas_.SetRenderCallback([this](Canvas::Canvas& c) { PaintCanvas(c); });
    canvas_.SetInputCallback([this](unsigned msg, int x, int y, int btn, int wheel,
                                    int key, int mods) {
        OnCanvasInput(msg, x, y, btn, wheel, key, mods);
        return true;
    });

    param_host_ = std::make_unique<NodeParamHost>();
    if (param_host_->Create(parent_hwnd_)) {
        param_host_->SetCommitHandler(
            [this](std::uint64_t n, const std::string& p, const std::string& t) {
                OnParamHostCommit(n, p, t);
            });
        param_host_->SetBrowseHandler(
            [this](std::uint64_t n, const std::string& p) {
                OnParamHostBrowse(n, p);
            });
    }

    // Autosave preference (persisted by the settings panel).
    {
        std::string v, err;
        if (XER::Cache::CacheManager::Instance().ReadPersistentText(
                "settings", "app", "autosave", &v, &err)) {
            autosave_enabled_ = !(v == "0" || v == "false");
        }
    }

    // Top navigation: graph editor / projects / settings.
    nav_tabs_ = std::make_shared<Tabs>();
    nav_tabs_->AddTab("Node Graph");
    nav_tabs_->AddTab("Projects");
    nav_tabs_->AddTab("Settings");
    nav_tabs_->AddTab("Video");
    nav_tabs_->SetActiveIndex(0);
    nav_tabs_->SetOnChanged([this](int index) { SetView(index); });

    back_btn_ = std::make_shared<Button>("‹ Projects", ButtonStyle::Ghost);
    back_btn_->SetTooltip("Back to the project browser");
    back_btn_->SetVisible(false);
    back_btn_->SetOnClick([this] {
        if (browser_back_hook_) browser_back_hook_();
    });

    status_label_ = std::make_shared<Label>("Ready", true);

    SettingsWidget::Hooks hooks;
    hooks.on_theme_changed = std::move(on_theme_changed);
    hooks.autosave_get = [this] { return autosave_enabled_; };
    hooks.autosave_set = [this](bool v) {
        autosave_enabled_ = v;
        std::string err;
        XER::Cache::CacheManager::Instance().WritePersistentText(
            "settings", "app", "autosave", v ? "1" : "0", &err);
        if (!v) {
            autosave_dirty_ = false;
        }
    };
    hooks.on_new_graph = [this] { NewGraph(); };
    hooks.on_save_graph = [this] { SaveGraph(); };
    hooks.on_load_graph = [this] { LoadGraph(); };
    hooks.graph_file_get = [this] { return pending_file_path_; };
    settings_widget_ = std::make_shared<SettingsWidget>(parent_hwnd_, std::move(hooks));
    settings_scroll_ = std::make_shared<ScrollArea>(settings_widget_);

    // Projects page: backed by the same ProjectStore as autosave.
    ProjectsWidget::Hooks phooks;
    phooks.on_open_project = [this](std::int64_t id) { OpenProjectById(id); };
    phooks.on_save_current_as =
        [this](const std::string& name) { SaveCurrentGraphAsProject(name); };
    phooks.on_notify = [this](const std::string& text, bool is_error) {
        SetStatus(text, is_error);
        Toast(text, is_error);
    };
    projects_widget_ = std::make_shared<ProjectsWidget>(parent_hwnd_, phooks);
    projects_scroll_ = std::make_shared<ScrollArea>(projects_widget_);

    // Video preview view — owns a XER::Content::Video::VideoEngine.
    video_widget_ = std::make_shared<VideoPreviewWidget>(parent_hwnd_);
    video_widget_->SetVisible(false);

    // Restore autosaved graph: prefer the SQLite project store, fall back
    // to the legacy cache-text blob.
    std::string saved;
    std::string err;
    if (NodeGraph::ProjectStore* store = EnsureProjectStore()) {
        const std::int64_t pid = AutosaveProjectId();
        if (pid >= 0 && store->LoadGraph(pid, "main", &saved, nullptr) &&
            !saved.empty()) {
            NG::Graph tmp;
            if (NG::DeserializeGraph(saved, &tmp, nullptr)) {
                *levels_.front().graph = tmp;
            }
        }
    }
    if (saved.empty() &&
        XER::Cache::CacheManager::Instance().ReadPersistentText(
            "ui", "node_graph_editor", "autosave", &saved, &err) && !saved.empty()) {
        NG::Graph tmp;
        if (NG::DeserializeGraph(saved, &tmp, nullptr)) {
            *levels_.front().graph = tmp;
        }
    }
    levels_.front().seen_revision = levels_.front().graph->revision();

    // Projects page shares the store opened above (may be null on failure).
    projects_widget_->SetStore(EnsureProjectStore());
    projects_widget_->Refresh();

    // Optional startup project (app shell passes -1 and opens via
    // OpenProjectById after creation; embedding/tests may pass an id).
    if (initial_project_id >= 0) {
        OpenProjectById(initial_project_id);
    }
}

void NodeGraphEditorWidget::SetBrowserBackHook(std::function<void()> hook) {
    browser_back_hook_ = std::move(hook);
    if (back_btn_) back_btn_->SetVisible(browser_back_hook_ != nullptr);
}

NodeGraphEditorWidget::~NodeGraphEditorWidget() {
    if (param_host_) param_host_->Destroy();
    if (autosave_dirty_ && autosave_enabled_) {
        FlushAutosave();
    }
}

NodeGraph::Graph& NodeGraphEditorWidget::RootGraph() { return *levels_.front().graph; }
const NodeGraph::Graph& NodeGraphEditorWidget::RootGraph() const { return *levels_.front().graph; }

// ── Layout / chrome ────────────────────────────────────────────────────────

void NodeGraphEditorWidget::Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) {
    const auto bounds = GetBounds();
    renderer.FillRect(bounds, tokens.background, tokens.radius_large);
}

void NodeGraphEditorWidget::Layout(const Layout::Rect& bounds) {
    // Reuse the last real theme tokens: a value-initialized Tokens has zero
    // spacing, which would lay the canvas out at pad=0 for a frame (visible
    // jump = flicker) if this overload is ever hit after a themed layout.
    Layout(bounds, canvas_tokens_valid_ ? canvas_tokens_
                                      : CachedLayoutTokens());
}

void NodeGraphEditorWidget::Layout(const Layout::Rect& bounds, const Theme::Tokens& tokens) {
    CacheLayoutTokens(tokens);
    SetBounds(bounds);
    // Capture real theme tokens for canvas painting (Layout(bounds) may be
    // invoked with a value-initialized Tokens — spacing is 0 then, keep the
    // last valid set instead).
    if (tokens.spacing_medium > 0.0f) {
        canvas_tokens_ = tokens;
        canvas_tokens_valid_ = true;
    }

    // Chrome children attach lazily on first layout — the widget must be
    // owned by a shared_ptr before AddChild/SetParent works, and mutating
    // children_ during Render would invalidate the render traversal.
    if (!chrome_attached_) {
        chrome_attached_ = true;
        AddChild(nav_tabs_);
        AddChild(back_btn_);
        AddChild(status_label_);
        AddChild(settings_scroll_);
        AddChild(projects_scroll_);
        AddChild(video_widget_);
    }
    if (!ctx_handler_set_) {
        if (WidgetTree* tree = GetTree()) {
            tree->SetContextMenuHandler(
                [this](const Event& e) { return OnChromeContextMenu(e); });
            ctx_handler_set_ = true;
        }
    }

    const float pad = tokens.spacing_medium;
    const float nav_h = 38.0f;
    const float status_h = 24.0f;

    // Top navigation tabs (+ optional "‹ Projects" back button on the right).
    const bool show_back = back_btn_ && back_btn_->IsVisible();
    const float back_w = show_back ? 96.0f : 0.0f;
    nav_tabs_->Layout(Layout::Rect{bounds.x + pad, bounds.y + pad * 0.4f,
                                   bounds.width - 2.0f * pad - back_w,
                                   nav_h}, CachedLayoutTokens());
    if (show_back) {
        back_btn_->Layout(Layout::Rect{
            bounds.x + bounds.width - pad - back_w,
            bounds.y + pad * 0.4f + (nav_h - 30.0f) * 0.5f,
            back_w, 30.0f}, CachedLayoutTokens());
    }

    const float content_top = bounds.y + pad * 0.4f + nav_h + tokens.spacing_medium;
    const float status_y = bounds.y + bounds.height - pad * 0.5f - status_h;
    const float content_h = std::max(60.0f, status_y - content_top - tokens.spacing_small);

    status_label_->Layout(Layout::Rect{bounds.x + pad + 4.0f, status_y + 3.0f,
                                       900.0f, status_h}, CachedLayoutTokens());

    if (view_ == 2) {
        // Settings view: scrollable panel fills the content area.
        settings_scroll_->SetVisible(true);
        settings_scroll_->Layout(
            Layout::Rect{bounds.x + pad, content_top,
                         bounds.width - 2.0f * pad, content_h}, tokens);
        projects_scroll_->SetVisible(false);
        video_widget_->SetVisible(false);
        video_widget_->SetPreviewActive(false);
        canvas_.SetVisible(false);
        if (param_host_) {
            param_host_->SetHostVisible(false);
            param_host_->Sync({});
        }
        return;
    }

    if (view_ == 1) {
        // Projects view: scrollable project list fills the content area.
        projects_scroll_->SetVisible(true);
        projects_scroll_->Layout(
            Layout::Rect{bounds.x + pad, content_top,
                         bounds.width - 2.0f * pad, content_h}, tokens);
        settings_scroll_->SetVisible(false);
        video_widget_->SetVisible(false);
        video_widget_->SetPreviewActive(false);
        canvas_.SetVisible(false);
        if (param_host_) {
            param_host_->SetHostVisible(false);
            param_host_->Sync({});
        }
        return;
    }

    if (view_ == 3) {
        // Video view: preview fills the content area (no scroll — the
        // canvas area must stay put for the letterboxed frame).
        video_widget_->SetVisible(true);
        video_widget_->SetPreviewActive(true);
        video_widget_->Layout(
            Layout::Rect{bounds.x + pad, content_top,
                         bounds.width - 2.0f * pad, content_h}, tokens);
        settings_scroll_->SetVisible(false);
        projects_scroll_->SetVisible(false);
        canvas_.SetVisible(false);
        if (param_host_) {
            param_host_->SetHostVisible(false);
            param_host_->Sync({});
        }
        return;
    }

    settings_scroll_->SetVisible(false);
    projects_scroll_->SetVisible(false);
    video_widget_->SetVisible(false);
    video_widget_->SetPreviewActive(false);
    canvas_.SetVisible(IsVisible());
    if (param_host_) {
        param_host_->SetHostVisible(IsVisible());
        if (!IsVisible()) param_host_->Sync({});
    }

    // Inspector column (right side), width animated in Tick. Widgets are
    // hidden first so collapsed/overflowing controls never render at stale
    // bounds over the canvas area.
    const float iw = inspector_width_;
    for (auto& w : inspector_widgets_) {
        w->SetVisible(false);
    }
    if (iw > 1.0f) {
        const float ix = bounds.x + bounds.width - pad - iw;
        float iy = content_top + tokens.spacing_medium;
        const float ih = status_y - iy - tokens.spacing_small;
        float cy = iy;
        for (auto& w : inspector_widgets_) {
            const float wh = std::max(0.0f, std::min(30.0f, ih - (cy - iy)));
            if (wh <= 0.0f) break;
            w->SetVisible(true);
            w->Layout(Layout::Rect{ix + 8.0f, cy, iw - 16.0f, wh}, CachedLayoutTokens());
            cy += wh + 4.0f;
            if (cy >= iy + ih) break;
        }
    }

    // Canvas covers the graph area between nav and status bar.
    const float cx = bounds.x + pad;
    const float cw = bounds.width - 2.0f * pad - iw - tokens.spacing_medium;
    canvas_.SetBounds(cx, content_top, std::max(50.0f, cw), std::max(50.0f, content_h));
    if (param_host_ && param_host_->IsCreated()) {
        param_host_->SetCanvasRect(cx, content_top,
                                   std::max(50.0f, cw),
                                   std::max(50.0f, content_h));
        // Field bg = slightly lifted surface; text = foreground.
        const auto mixc = [](const Theme::Color& a, const Theme::Color& b,
                             float t) {
            return Theme::Color{a.r + (b.r - a.r) * t,
                                a.g + (b.g - a.g) * t,
                                a.b + (b.b - a.b) * t, 1.0f};
        };
        const Theme::Color field = mixc(tokens.surface, tokens.foreground, 0.07f);
        param_host_->SetColors(field.r, field.g, field.b,
                               tokens.foreground.r, tokens.foreground.g,
                               tokens.foreground.b,
                               tokens.border.r, tokens.border.g, tokens.border.b);
        param_host_->Sync({});  // hide stale controls until next paint sync
    }
}

// ── Theme colors for canvas ────────────────────────────────────────────────

const Theme::Tokens& NodeGraphEditorWidget::CT() const {
    // Fallback mirrors the dark theme if Layout never delivered tokens.
    static const Theme::Tokens kFallback{
        .background = {0.04f, 0.04f, 0.04f, 1.0f},
        .foreground = {0.98f, 0.98f, 0.98f, 1.0f},
        .surface = {0.09f, 0.09f, 0.11f, 1.0f},
        .muted = {0.63f, 0.63f, 0.67f, 1.0f},
        .border = {0.15f, 0.15f, 0.17f, 1.0f},
        .accent = {0.39f, 0.40f, 0.95f, 1.0f},
        .accent_foreground = {1.0f, 1.0f, 1.0f, 1.0f},
        .focus = {0.98f, 0.98f, 0.98f, 1.0f},
        .secondary = {0.13f, 0.13f, 0.15f, 1.0f},
        .hover = {0.15f, 0.15f, 0.17f, 1.0f},
        .ring = {0.39f, 0.40f, 0.95f, 1.0f},
        .danger = {0.94f, 0.27f, 0.27f, 1.0f},
        .danger_foreground = {1.0f, 1.0f, 1.0f, 1.0f},
        .radius_small = 6.0f, .radius_medium = 8.0f, .radius_large = 12.0f,
        .spacing_small = 8.0f, .spacing_medium = 16.0f, .spacing_large = 24.0f,
        .font_small = 12.0f, .font_body = 14.0f, .font_title = 30.0f,
    };
    return canvas_tokens_valid_ ? canvas_tokens_ : kFallback;
}

Canvas::Canvas::Color NodeGraphEditorWidget::TC(const Theme::Color& c) const {
    return {c.r, c.g, c.b, c.a};
}

Canvas::Canvas::Color NodeGraphEditorWidget::WithAlpha(const Theme::Color& c,
                                                       float a) const {
    return {c.r, c.g, c.b, a};
}

Canvas::Canvas::Color NodeGraphEditorWidget::AccentC(float a) const {
    return WithAlpha(CT().accent, a);
}

Canvas::Canvas::Color NodeGraphEditorWidget::DangerC(float a) const {
    return WithAlpha(CT().danger, a);
}

Canvas::Canvas::Color NodeGraphEditorWidget::WarnC(float a) const {
    return {0.96f, 0.70f, 0.25f, a};  // amber — validation warnings
}

Theme::Color NodeGraphEditorWidget::Mix(const Theme::Color& a,
                                        const Theme::Color& b,
                                        float t) const {
    t = std::clamp(t, 0.0f, 1.0f);
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t,
            a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}

Canvas::Canvas::Color NodeGraphEditorWidget::PopoverBg() const {
    const auto& t = CT();
    // Luminance of the theme background decides whether the popover should
    // sit slightly above (dark) or stay plain white (light).
    const float lum = 0.2126f * t.background.r + 0.7152f * t.background.g +
                      0.0722f * t.background.b;
    auto bg = TC(Mix(t.surface, t.foreground, lum > 0.5f ? 0.0f : 0.05f));
    bg.a *= 0.96f;  // translucent popover, Tailwind glass feel
    return bg;
}

// ── Geometry ───────────────────────────────────────────────────────────────

void NodeGraphEditorWidget::RebuildGeometries() {
    geoms_.clear();
    world_min_x_ = world_min_y_ = 1e30;
    world_max_x_ = world_max_y_ = -1e30;

    const double z = Current().zoom;
    const double px = Current().pan_x;
    const double py = Current().pan_y;
    canvas_w_ = canvas_.GetWidth();
    canvas_h_ = canvas_.GetHeight();

    for (const auto& [id, node] : Current().graph->nodes()) {
        NodeGeom g;
        g.x = node.x;
        g.y = node.y;
        g.w = kNodeW;

        auto it = drag_offset_.find(id);
        if (it != drag_offset_.end()) {
            g.x += it->second.first;
            g.y += it->second.second;
        }

        const double in_rows = static_cast<double>(node.inputs.size());
        const double out_rows = static_cast<double>(node.outputs.size());
        const double content_rows = std::max(in_rows, out_rows);

        // Parameter rows: spec order first (registry), then any stored
        // params missing from the spec. Each row is an interactive
        // in-node control — see DrawNodes / HitParam.
        const NG::NodeTypeDescriptor* pdesc =
            Current().graph->registry().Find(node.type);
        g.desc = pdesc;
        std::vector<std::pair<std::string, const NG::ParamSpec*>> ordered;
        std::set<std::string> seen;
        if (pdesc != nullptr) {
            for (const auto& spec : pdesc->params) {
                ordered.emplace_back(spec.name, &spec);
                seen.insert(spec.name);
            }
        }
        for (const auto& [pname, pval] : node.params) {
            if (seen.insert(pname).second) ordered.emplace_back(pname, nullptr);
        }
        // Collapsed nodes keep only ports — param rows are hidden.
        const bool collapsed = Current().collapsed_nodes.count(id) != 0;
        // +1 row when params overflow — hosts the "+N more" line.
        const double param_rows = collapsed
            ? 0.0
            : std::min(static_cast<double>(ordered.size()),
                       static_cast<double>(kMaxParamRows)) +
                  (ordered.size() > static_cast<size_t>(kMaxParamRows) ? 1.0 : 0.0);
        g.h = kHeaderH + content_rows * kRowH + param_rows * kParamRowH + 8.0;

        double ry = g.y + kHeaderH + kRowH * 0.5;
        for (const auto& in : node.inputs) {
            g.ports.push_back({in.name, false, ry});
            ry += kRowH;
        }
        ry = g.y + kHeaderH + kRowH * 0.5;
        for (const auto& out : node.outputs) {
            g.ports.push_back({out.name, true, ry});
            ry += kRowH;
        }

        double param_y = g.y + kHeaderH + content_rows * kRowH + 3.0;
        for (size_t i = 0; !collapsed && i < ordered.size() &&
                          i < static_cast<size_t>(kMaxParamRows); ++i) {
            const std::string& pname = ordered[i].first;
            const NG::ParamSpec* spec = ordered[i].second;
            const NG::Value cur = node.params.count(pname)
                ? node.params.at(pname)
                : (spec ? spec->default_value : NG::Value());
            int kind = 3;
            if (spec != nullptr && !spec->options.empty()) {
                kind = 2;
            } else if (!cur.IsNone() && cur.Type() == NG::ValueType::kBool) {
                kind = 1;
            } else if (!cur.IsNone() && cur.Type() == NG::ValueType::kNumber) {
                kind = 0;
            } else if (node.type == "io.file_open" && pname == "path") {
                kind = 4;
            }
            const bool browse = kind == 4;
            g.params.push_back({pname, kind, param_y, kParamRowH, browse});
            param_y += kParamRowH;
        }
        geoms_[id] = g;

        world_min_x_ = std::min(world_min_x_, g.x);
        world_min_y_ = std::min(world_min_y_, g.y);
        world_max_x_ = std::max(world_max_x_, g.x + g.w);
        world_max_y_ = std::max(world_max_y_, g.y + g.h);
    }

    // Link curves in screen space — one pass per paint, shared by the
    // renderer and hit-testing.
    link_geoms_.clear();
    for (const auto& [lid, link] : Current().graph->links()) {
        double x1, y1, x2, y2;
        if (!PortScreenPos(link.from.node, link.from.port, true, x1, y1) ||
            !PortScreenPos(link.to.node, link.to.port, false, x2, y2)) {
            continue;
        }
        LinkGeom lg;
        const double dx = std::max(48.0, std::abs(x2 - x1) * 0.45);
        lg.x1 = static_cast<float>(x1); lg.y1 = static_cast<float>(y1);
        lg.x2 = static_cast<float>(x2); lg.y2 = static_cast<float>(y2);
        lg.cx1 = static_cast<float>(x1 + dx); lg.cy1 = lg.y1;
        lg.cx2 = static_cast<float>(x2 - dx); lg.cy2 = lg.y2;
        lg.minx = std::min({lg.x1, lg.x2, lg.cx1, lg.cx2}) - 10.0f;
        lg.miny = std::min({lg.y1, lg.y2, lg.cy1, lg.cy2}) - 10.0f;
        lg.maxx = std::max({lg.x1, lg.x2, lg.cx1, lg.cx2}) + 10.0f;
        lg.maxy = std::max({lg.y1, lg.y2, lg.cy1, lg.cy2}) + 10.0f;
        link_geoms_[lid] = lg;
    }

    if (world_min_x_ > world_max_x_) {
        world_min_x_ = world_min_y_ = 0.0;
        world_max_x_ = world_max_y_ = 1.0;
    }
    (void)z; (void)px; (void)py;
}

double NodeGraphEditorWidget::ToScreenX(double wx) const {
    return wx * Current().zoom + Current().pan_x;
}
double NodeGraphEditorWidget::ToScreenY(double wy) const {
    return wy * Current().zoom + Current().pan_y;
}
double NodeGraphEditorWidget::ToWorldX(double sx) const {
    return (sx - Current().pan_x) / Current().zoom;
}
double NodeGraphEditorWidget::ToWorldY(double sy) const {
    return (sy - Current().pan_y) / Current().zoom;
}

bool NodeGraphEditorWidget::PortScreenPos(NodeGraph::NodeId nid,
                                        const std::string& port,
                                        bool is_output, double& sx, double& sy) const {
    auto git = geoms_.find(nid);
    if (git == geoms_.end()) return false;
    const NodeGeom& g = git->second;
    for (const auto& p : g.ports) {
        if (p.name == port && p.is_output == is_output) {
            sx = ToScreenX(is_output ? g.x + g.w : g.x);
            sy = ToScreenY(p.y);
            return true;
        }
    }
    return false;
}

// ── Paint ──────────────────────────────────────────────────────────────────

void NodeGraphEditorWidget::PaintCanvas(Canvas::Canvas& c) {
    RebuildGeometries();
    c.Clear(TC(CT().background));
    DrawGrid(c);
    DrawGroups(c);
    DrawLinks(c);
    DrawNodes(c);
    DrawSelectionFrame(c);
    DrawTempLink(c);
    DrawBoxSelect(c);
    DrawMinimap(c);
    DrawPalette(c);
    DrawMenu(c);
    DrawRenameField(c);
    DrawToast(c);
    DrawChromeOverlay(c);
    SyncParamHost();
}

// ── HWND param widgets (NodeParamHost) ─────────────────────────────────────

void NodeGraphEditorWidget::SetHostVisible(bool visible) {
    if (visible) {
        canvas_.SetVisible(view_ == 0 && IsVisible());
        if (param_host_) param_host_->SetHostVisible(view_ == 0);
    } else {
        canvas_.SetVisible(false);
        if (param_host_) param_host_->SetHostVisible(false);
    }
}

void NodeGraphEditorWidget::SyncParamHost() {
    if (param_host_ == nullptr || !param_host_->IsCreated()) return;
    const double z = Current().zoom;
    const double zc = std::clamp(z, 0.55, 1.8);
    const float pz = static_cast<float>(zc);
    std::vector<ParamCtrlSpec> specs;
    // Compact mode (zc < 0.8) draws params as text — no HWNDs then.
    if (zc >= 0.8) {
        for (const auto& [nid, g] : geoms_) {
            const NG::Node* node = Current().graph->FindNode(nid);
            if (node == nullptr) continue;
            for (const auto& pg : g.params) {
                // Same value/spec resolution as DrawNodes.
                NG::Value pv;
                const auto pit = node->params.find(pg.name);
                if (pit != node->params.end()) pv = pit->second;
                else if (g.desc != nullptr) {
                    for (const auto& sp : g.desc->params) {
                        if (sp.name == pg.name) { pv = sp.default_value; break; }
                    }
                }
                ParamCtrlSpec s;
                s.node = nid;
                s.param = pg.name;
                s.kind = pg.kind;
                const float row_x = static_cast<float>(ToScreenX(g.x)) + 7.0f;
                const float row_w = static_cast<float>(g.w * z) - 14.0f;
                const float row_y = static_cast<float>(ToScreenY(pg.y));
                const float row_h = static_cast<float>(pg.h * z);
                const float ry = row_y + 2.0f, rh = row_h - 4.0f;
                if (pg.kind == 1) {  // bool — cover the checkbox zone
                    const float bs = 10.0f * pz;
                    s.x = row_x + 1.0f;
                    s.y = row_y + (row_h - bs) * 0.5f - 1.0f;
                    s.w = bs + 8.0f;
                    s.h = bs + 8.0f;
                    s.text = (!pv.IsNone() && pv.AsBool()) ? "1" : "0";
                } else if (pg.kind == 2) {  // options — combo over value zone
                    s.x = row_x + row_w * 0.40f;
                    s.y = ry; s.w = row_w * 0.60f - 4.0f; s.h = rh;
                    s.text = pv.IsNone() ? std::string() : pv.AsString();
                    if (g.desc != nullptr) {
                        for (const auto& sp : g.desc->params) {
                            if (sp.name == pg.name) { s.options = sp.options; break; }
                        }
                    }
                } else if (pg.kind == 4) {  // file — EDIT + "…" aux button
                    const float nw = EstTextW(pg.name, 9.0f * pz);
                    s.x = row_x + nw + 12.0f;
                    s.y = ry;
                    s.w = row_w - nw - 12.0f - 30.0f;
                    s.h = rh;
                    s.text = pv.IsNone() ? std::string() : pv.AsString();
                } else if (pg.kind == 0) {  // number — EDIT over value zone
                    s.x = row_x + row_w * 0.42f;
                    s.y = ry; s.w = row_w * 0.58f - 4.0f; s.h = rh;
                    s.text = FormatValue(pv);
                } else {  // text/other — EDIT after "name ="
                    const float nw = EstTextW(pg.name, 9.5f * pz);
                    s.x = row_x + nw + 16.0f;
                    s.y = ry;
                    s.w = row_x + row_w - 4.0f - s.x;
                    s.h = rh;
                    s.text = FormatValue(pv);
                }
                if (s.w >= 24.0f && s.h >= 10.0f) specs.push_back(std::move(s));
            }
        }
    }
    param_host_->Sync(specs);
}

void NodeGraphEditorWidget::OnParamHostCommit(std::uint64_t nid,
                                              const std::string& pname,
                                              const std::string& text) {
    const NG::Node* node = Current().graph->FindNode(nid);
    if (node == nullptr) return;
    int kind = 3;
    const auto git = geoms_.find(nid);
    if (git != geoms_.end()) {
        for (const auto& pg : git->second.params) {
            if (pg.name == pname) { kind = pg.kind; break; }
        }
    }
    NG::Value v;
    if (kind == 0) {
        char* end = nullptr;
        const double d = std::strtod(text.c_str(), &end);
        if (end == text.c_str()) return;  // partial/empty input — keep old
        v = NG::Value(d);
    } else if (kind == 1) {
        v = NG::Value(text == "1" || text == "true");
    } else {
        v = NG::Value(text);
    }
    Current().history->SetParam(nid, pname, v);
    ScheduleAutosave();
    canvas_.Invalidate();
}

void NodeGraphEditorWidget::OnParamHostBrowse(std::uint64_t nid,
                                              const std::string& pname) {
    OpenParamFileBrowse(nid, pname);
}

void NodeGraphEditorWidget::DrawGrid(Canvas::Canvas& c) {
    const double z = Current().zoom;
    double step = 32.0;
    while (step * z < 26.0) step *= 2.0;
    const double x0 = ToWorldX(-40.0), x1 = ToWorldX(canvas_w_ + 40.0);
    const double y0 = ToWorldY(-40.0), y1 = ToWorldY(canvas_h_ + 40.0);
    const auto grid_col = WithAlpha(CT().foreground, 0.05f);
    // Dot grid (Blender-style): cheaper-looking and reads better at a
    // glance than hairlines. Falls back to lines when zoomed far out and
    // the dot count would explode.
    const double dot_step = step;
    const double dx0 = std::floor(x0 / dot_step) * dot_step;
    const double dy0 = std::floor(y0 / dot_step) * dot_step;
    const double dots_x = (x1 - dx0) / dot_step + 1.0;
    const double dots_y = (y1 - dy0) / dot_step + 1.0;
    if (dots_x * dots_y <= 800.0) {
        const auto dot_col = WithAlpha(CT().foreground, 0.10f);
        for (double wx = dx0; wx <= x1; wx += dot_step) {
            const float sx = static_cast<float>(ToScreenX(wx));
            for (double wy = dy0; wy <= y1; wy += dot_step) {
                const float sy = static_cast<float>(ToScreenY(wy));
                c.FillCircle(sx, sy, 1.1f, dot_col);
            }
        }
    } else {
        for (double wx = dx0; wx <= x1; wx += dot_step) {
            const float sx = static_cast<float>(ToScreenX(wx));
            c.DrawLine(sx, 0.0f, sx, static_cast<float>(canvas_h_), grid_col, 1.0f);
        }
        for (double wy = dy0; wy <= y1; wy += dot_step) {
            const float sy = static_cast<float>(ToScreenY(wy));
            c.DrawLine(0.0f, sy, static_cast<float>(canvas_w_), sy, grid_col, 1.0f);
        }
    }
    // Axes through the world origin.
    const auto axis_col = AccentC(0.22f);
    const float ax = static_cast<float>(ToScreenX(0.0));
    const float ay = static_cast<float>(ToScreenY(0.0));
    if (ax >= 0 && ax <= canvas_w_) {
        c.DrawLine(ax, 0.0f, ax, static_cast<float>(canvas_h_), axis_col, 1.0f);
    }
    if (ay >= 0 && ay <= canvas_h_) {
        c.DrawLine(0.0f, ay, static_cast<float>(canvas_w_), ay, axis_col, 1.0f);
    }
}

namespace {
// "#RRGGBB" -> canvas color (alpha 1); falls back to muted gray.
Canvas::Canvas::Color ParseGroupColor(const std::string& hex,
                                      const Theme::Tokens& t) {
    const auto fallback = Canvas::Canvas::Color{
        t.muted.r, t.muted.g, t.muted.b, t.muted.a};
    if (hex.size() < 7 || hex[0] != '#') return fallback;
    auto hv = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    int r = 0, g = 0, b = 0;
    for (int i = 0; i < 6; ++i) {
        const int d = hv(hex[1 + i]);
        if (d < 0) return fallback;
        if (i < 2) r = r * 16 + d;
        else if (i < 4) g = g * 16 + d;
        else b = b * 16 + d;
    }
    return {r / 255.0f, g / 255.0f, b / 255.0f, 1.0f};
}
} // namespace

void NodeGraphEditorWidget::DrawGroups(Canvas::Canvas& c) {
    const double z = Current().zoom;
    const auto& t = CT();
    for (const auto& [gid, grp] : Current().graph->groups()) {
        const float x = static_cast<float>(ToScreenX(grp.x));
        const float y = static_cast<float>(ToScreenY(grp.y));
        const float w = static_cast<float>(grp.w * z);
        const float h = static_cast<float>(grp.h * z);
        if (x + w < -40.0f || x > canvas_w_ + 40.0f ||
            y + h < -40.0f || y > canvas_h_ + 40.0f) {
            continue;
        }
        auto col = ParseGroupColor(grp.color, t);
        auto fill = col;
        fill.a = 0.08f;
        c.FillRect({x, y, w, h}, fill, 10.0f);
        col.a = 0.45f;
        c.StrokeRect({x, y, w, h}, col, 1.0f, 10.0f);
        if (!grp.title.empty()) {
            col.a = 0.85f;
            c.DrawText(grp.title, x + 10.0f, y + 7.0f,
                       11.0f, col);
        }
    }
}

void NodeGraphEditorWidget::DrawLinks(Canvas::Canvas& c) {
    const auto& links = Current().graph->links();
    for (const auto& [id, link] : links) {
        const auto git = link_geoms_.find(id);
        if (git == link_geoms_.end()) continue;
        const LinkGeom& lg = git->second;
        // Viewport culling from the cached curve bbox (control points
        // included) — skips the draw calls for off-screen wires entirely.
        if (lg.maxx < -40.0f || lg.minx > canvas_w_ + 40.0f ||
            lg.maxy < -40.0f || lg.miny > canvas_h_ + 40.0f) {
            continue;
        }
        // Port type color from the source node's output spec.
        NG::ValueType vt = NG::ValueType::kAny;
        if (const NG::Node* n = Current().graph->FindNode(link.from.node)) {
            if (const auto* spec = n->FindOutput(link.from.port)) vt = spec->type;
        }
        auto col = PortColor(vt);
        col.a *= 0.8f;
        const bool sel = Current().selected_link && *Current().selected_link == id;
        const bool hov = hovered_link_ == id && !sel;
        if (sel) col = AccentC(0.95f);
        else if (hov) col.a = std::min(1.0f, col.a + 0.25f);
        const float lw = sel ? 1.25f : (hov ? 1.1f : 1.0f);
        // Two-pass "tube": dark underlay separates the wire from the
        // background, colored core carries the type/selection color.
        const auto under = Canvas::Canvas::Color{0.02f, 0.03f, 0.05f, 0.55f};
        c.DrawCubicBezier(lg.x1, lg.y1, lg.cx1, lg.cy1, lg.cx2, lg.cy2,
                          lg.x2, lg.y2, under, (sel ? 4.6f : 3.4f) * lw);
        c.DrawCubicBezier(lg.x1, lg.y1, lg.cx1, lg.cy1, lg.cx2, lg.cy2,
                          lg.x2, lg.y2, col, (sel ? 2.6f : 1.7f) * lw);
        // Bright pinprick at both ends where the wire meets the ports.
        c.FillCircle(lg.x1, lg.y1, sel ? 3.0f : (hov ? 2.6f : 2.2f), col);
        c.FillCircle(lg.x2, lg.y2, sel ? 3.0f : (hov ? 2.6f : 2.2f), col);
    }
}

void NodeGraphEditorWidget::DrawTempLink(Canvas::Canvas& c) {
    if (!link_dragging_) return;
    double x1, y1;
    const bool ok = link_from_is_output_
        ? PortScreenPos(link_from_.node, link_from_.port, true, x1, y1)
        : PortScreenPos(link_from_.node, link_from_.port, false, x1, y1);
    if (!ok) return;
    // Compatibility preview against the port under the cursor. The drop
    // port was resolved once on the last mousemove (HandleMouseMove);
    // CanConnect walks the graph (cycle check) so it stays throttled at
    // ~12 Hz keyed by drop target.
    const auto drop = drop_valid_ ? drop_port_ : NG::PortRef{};
    static thread_local NodeGraph::NodeId cn_drop_node = 0;
    static thread_local std::string cn_drop_port;
    static thread_local DWORD cn_last = 0;
    static thread_local bool cn_ok = false;
    const DWORD now_ms = GetTickCount();
    const bool key_changed = cn_drop_node != drop.node ||
                             cn_drop_port != drop.port;
    if (key_changed || now_ms - cn_last > 80) {
        cn_drop_node = drop.node;
        cn_drop_port = drop.port;
        cn_ok = drop.node != NG::kInvalidNode;
        if (cn_ok) {
            const NG::PortRef from = link_from_is_output_ ? link_from_ : drop;
            const NG::PortRef to = link_from_is_output_ ? drop : link_from_;
            std::string reason;
            cn_ok = Current().graph->CanConnect(from, to, &reason);
        }
        cn_last = now_ms;
    }
    auto col = cn_ok ? Canvas::Canvas::Color{0.13f, 0.77f, 0.37f, 0.95f}
                     : AccentC(0.9f);
    if (drop.node == NG::kInvalidNode) {
        col = AccentC(0.9f);
    } else if (!cn_ok) {
        col = DangerC();
    }
    const float x2 = static_cast<float>(mouse_sx_);
    const float y2 = static_cast<float>(mouse_sy_);
    const float dx = std::max(40.0f, std::abs(x2 - static_cast<float>(x1)) * 0.45f);
    c.DrawCubicBezier(static_cast<float>(x1), static_cast<float>(y1),
                      static_cast<float>(x1) + dx, static_cast<float>(y1),
                      x2 - dx, y2, x2, y2, col, 2.0f, 6.0f, 5.0f);
}

void NodeGraphEditorWidget::DrawNodes(Canvas::Canvas& c) {
    Issues();  // refresh validation cache for this revision
    const double z = Current().zoom;
    const double zc = std::clamp(z, 0.55, 1.8);
    for (const auto& [id, node] : Current().graph->nodes()) {
        auto git = geoms_.find(id);
        if (git == geoms_.end()) continue;
        const NodeGeom& g = git->second;
        const float rx = static_cast<float>(ToScreenX(g.x));
        const float ry = static_cast<float>(ToScreenY(g.y));
        const float rw = static_cast<float>(g.w * z);
        const float rh = static_cast<float>(g.h * z);
        // Viewport culling: skip nodes fully outside the canvas — at low
        // zoom or large graphs this avoids most of the draw calls.
        if (rx + rw < -60.0f || rx > canvas_w_ + 60.0f ||
            ry + rh < -60.0f || ry > canvas_h_ + 60.0f) {
            continue;
        }

        const NG::NodeTypeDescriptor* desc = g.desc;
        const bool selected = Current().selected_nodes.count(id) != 0;
        const bool is_sub = node.type == "graph.subgraph";
        const bool has_error = error_nodes_.count(id) != 0;
        const bool hovered = id == hovered_node_;

        // Body — shadcn card: surface fill, hairline border, accent ring
        // when selected, danger ring on error.
        const auto& t = CT();
        c.FillRect({rx, ry, rw, rh},
                   TC(selected ? Mix(t.surface, t.accent, 0.10f) : t.surface),
                   8.0f);
        if (selected) {
            // Soft accent glow ring outside the border.
            c.StrokeRect({rx - 1.5f, ry - 1.5f, rw + 3.0f, rh + 3.0f},
                         AccentC(0.35f), 3.0f, 9.0f);
        }
        if (debug_nodes_.count(id)) {
            // Step-debug: nodes evaluated in the last step get a
            // distinct outer ring (kept until the next step/run).
            c.StrokeRect({rx - 3.5f, ry - 3.5f, rw + 7.0f, rh + 7.0f},
                         WarnC(), 1.8f, 10.0f);
        }
        c.StrokeRect({rx, ry, rw, rh},
                     has_error ? DangerC()
                     : selected ? AccentC(1.0f)
                     : hovered ? TC(Mix(t.border, t.foreground, 0.35f))
                     : TC(t.border),
                     selected || has_error ? 1.6f : hovered ? 1.4f : 1.0f,
                     8.0f);
        if (is_sub) {
            c.StrokeRect({rx + 3.0f, ry + 3.0f, rw - 6.0f, rh - 6.0f},
                         AccentC(0.5f), 1.0f, 6.0f);
        }

        // Studio header: category-tinted band, color-coded hairline,
        // category tag on the right.
        const std::string cat = desc ? desc->category : std::string();
        const auto cat_col = CategoryColor(cat, node.type);
        const float hh = static_cast<float>(kHeaderH * z);
        auto band = cat_col;
        band.a = 0.14f;
        c.FillRect({rx + 1.0f, ry + 1.0f, rw - 2.0f, hh - 1.0f}, band, 6.0f);
        const float hy = ry + hh;
        auto sep = cat_col;
        sep.a = 0.38f;
        c.DrawLine(rx + 8.0f, hy, rx + rw - 8.0f, hy, sep, 1.0f);
        std::string title = !node.title.empty() ? node.title
                              : (desc ? desc->title : node.type);
        if (title.size() > 26) title = title.substr(0, 25) + "…";
        c.DrawText(title, rx + 12.0f, ry + 18.0f * static_cast<float>(zc),
                   12.0f * static_cast<float>(zc), TC(t.foreground));
        const auto iit = node_issue_sev_.find(id);
        const bool has_issue = iit != node_issue_sev_.end();
        const float badge_d = 16.0f * static_cast<float>(zc);
        if (rw > 150.0f && !cat.empty()) {
            auto tag = cat_col;
            tag.a = 0.95f;
            std::string ct = cat;
            if (ct.size() > 12) ct = ct.substr(0, 11) + "…";
            const float est = EstTextW(ct, 8.0f * static_cast<float>(zc));
            c.DrawText(ct, rx + rw - est - 10.0f -
                           (has_issue ? badge_d + 4.0f : 0.0f),
                       ry + 16.0f * static_cast<float>(zc),
                       8.0f * static_cast<float>(zc), tag);
        }
        // Validation badge: red "!" for errors, amber for warnings —
        // click jumps via the Problems menu.
        if (has_issue) {
            const bool is_err =
                iit->second == NG::ValidationIssue::Severity::kError;
            const float br = badge_d * 0.5f;
            const float bcx = rx + rw - br - 4.0f;
            const float bcy = ry + hh * 0.5f;
            c.FillCircle(bcx, bcy, br, is_err ? DangerC() : WarnC());
            c.DrawText("!", bcx - 3.0f * static_cast<float>(zc),
                       bcy - 8.0f * static_cast<float>(zc),
                       11.0f * static_cast<float>(zc),
                       Canvas::Canvas::Color{1.0f, 1.0f, 1.0f, 1.0f});
        }
        if (is_sub) {
            c.DrawText("SUBGRAPH", rx + rw - 72.0f,
                       ry + 17.0f * static_cast<float>(zc),
                       9.0f * static_cast<float>(zc), AccentC(0.9f));
        }
        // Collapse chevron — click toggles via the node menu.
        const bool collapsed = Current().collapsed_nodes.count(id) != 0;
        if (rw > 120.0f) {
            const float cx = rx + rw - (has_issue ? badge_d + 4.0f : 0.0f)
                             - (rw > 150.0f && !cat.empty() ? 44.0f : 0.0f)
                             - 12.0f;
            c.DrawText(collapsed ? "▸" : "▾", cx,
                       ry + 16.0f * static_cast<float>(zc),
                       11.0f * static_cast<float>(zc), TC(t.muted));
        }

        // Ports.
        for (const auto& p : g.ports) {
            const float py = static_cast<float>(ToScreenY(p.y));
            const float px = p.is_output
                ? rx + rw - 2.0f
                : rx + 2.0f;
            NG::ValueType pt = NG::ValueType::kAny;
            const auto& specs = p.is_output ? node.outputs : node.inputs;
            for (const auto& s : specs) {
                if (s.name == p.name) { pt = s.type; break; }
            }
            // Ring + core: reads as a real socket on the node edge.
            c.FillCircle(px, py, 5.2f, Canvas::Canvas::Color{0.02f, 0.03f, 0.05f, 0.9f});
            c.FillCircle(px, py, 3.8f, PortColor(pt));
            const float tx = p.is_output ? px - 8.0f : px + 8.0f;
            const auto& label = p.name;
            // Right-align output labels crudely by measuring ~7px/char.
            const float est_w = static_cast<float>(label.size()) * 6.2f * static_cast<float>(zc);
            const float draw_x = p.is_output ? tx - est_w : tx;
            c.DrawText(label, draw_x, py + 4.0f, 10.0f * static_cast<float>(zc),
                       TC(Mix(t.muted, t.foreground, 0.35f)));
        }

        // Param rows — interactive in-node controls (number drag, bool
        // toggle, option cycle, file browse). Edited values commit through
        // History::SetParam so they stay undoable and autosave-scheduled.
        auto find_spec = [&](const std::string& pname) -> const NG::ParamSpec* {
            if (desc == nullptr) return nullptr;
            for (const auto& s : desc->params) {
                if (s.name == pname) return &s;
            }
            return nullptr;
        };
        auto param_value = [&](const std::string& pname) -> NG::Value {
            const auto it = node.params.find(pname);
            if (it != node.params.end()) return it->second;
            if (const NG::ParamSpec* s = find_spec(pname)) return s->default_value;
            return NG::Value();
        };
        const float pz = static_cast<float>(zc);
        // Zoomed far out the per-param widgets cost more than they earn —
        // fall back to one summary line per param (CPU raster surface).
        const bool compact_params = zc < 0.8;
        for (const auto& pg : g.params) {
            const float row_x = rx + 7.0f;
            const float row_w = rw - 14.0f;
            const float row_y = static_cast<float>(ToScreenY(pg.y));
            const float row_h = static_cast<float>(pg.h * z);
            const float ty = row_y + row_h * 0.5f + 3.5f * pz;
            const NG::Value pv = param_value(pg.name);
            const bool active =
                param_node_ == id && param_name_ == pg.name && drag_mode_ == DragMode::ParamDrag;
            const auto val_col = active ? AccentC(1.0f) : TC(t.foreground);

            if (compact_params) {
                std::string line = pg.name + "  " + FormatValue(pv);
                if (line.size() > 30) line = line.substr(0, 29) + "…";
                c.DrawText(line, row_x + 2.0f, ty, 9.0f * pz, TC(t.muted));
                continue;
            }

            if (pg.kind == 1) {  // bool — checkbox + label
                const float bx = row_x + 2.0f;
                const float bs = 10.0f * pz;
                const float by = row_y + (row_h - bs) * 0.5f;
                const bool on = !pv.IsNone() && pv.AsBool();
                c.StrokeRect({bx, by, bs, bs}, TC(t.border), 1.0f, 3.0f);
                if (on) {
                    c.FillRect({bx + 1.5f, by + 1.5f, bs - 3.0f, bs - 3.0f},
                               AccentC(0.95f), 2.5f);
                    // Check mark.
                    const auto chk = TC(t.background);
                    c.DrawLine(bx + bs * 0.22f, by + bs * 0.52f,
                               bx + bs * 0.44f, by + bs * 0.74f, chk, 1.6f);
                    c.DrawLine(bx + bs * 0.44f, by + bs * 0.74f,
                               bx + bs * 0.80f, by + bs * 0.28f, chk, 1.6f);
                }
                c.DrawText(pg.name, bx + bs + 7.0f, ty, 9.5f * pz, TC(t.muted));
            } else if (pg.kind == 2) {  // options — < value > cycle
                std::string val = FormatValue(pv);
                if (val.size() > 12) val = val.substr(0, 11) + "…";
                const float vw = EstTextW(val, 9.5f * pz);
                const float gx = row_x + row_w - vw - 22.0f * pz;
                c.DrawText(pg.name, row_x + 2.0f, ty, 9.5f * pz, TC(t.muted));
                c.DrawText("<", gx, ty, 9.5f * pz, TC(t.muted));
                c.DrawText(val, gx + 9.0f * pz, ty, 9.5f * pz, val_col);
                c.DrawText(">", gx + 9.0f * pz + vw + 2.0f, ty, 9.5f * pz,
                           TC(t.muted));
            } else if (pg.kind == 4) {  // file path — text + browse button
                c.DrawText(pg.name, row_x + 2.0f, ty, 9.0f * pz, TC(t.muted));
                std::string path = pv.IsNone() ? std::string("no file")
                                                : pv.AsString();
                const float max_w = row_w - 34.0f * pz - EstTextW(pg.name, 9.0f * pz) - 8.0f;
                const float avail = std::max(20.0f, max_w);
                while (!path.empty() && EstTextW(path, 9.0f * pz) > avail) {
                    path = path.substr(0, path.size() - 2);
                }
                if (!path.empty() && EstTextW(path, 9.0f * pz) > avail) path += "…";
                const float px = row_x + 4.0f + EstTextW(pg.name, 9.0f * pz) + 5.0f;
                c.DrawText(path, px, ty, 9.0f * pz,
                           pv.IsNone() ? TC(t.muted) : val_col);
                // "…" browse button.
                const float bw = 22.0f * pz, bh = row_h - 6.0f * pz;
                const float bx = row_x + row_w - bw - 1.0f;
                const float by = row_y + 3.0f * pz;
                c.FillRect({bx, by, bw, bh}, TC(Mix(t.secondary, t.foreground, 0.06f)), 4.0f);
                c.StrokeRect({bx, by, bw, bh}, TC(t.border), 1.0f, 4.0f);
                const float ew = EstTextW("…", 10.0f * pz);
                c.DrawText("…", bx + (bw - ew) * 0.5f, by + bh * 0.5f + 3.5f * pz,
                           10.0f * pz, TC(t.foreground));
            } else if (pg.kind == 0) {  // number — drag-adjust row
                std::string val = FormatValue(pv);
                const float vw = EstTextW(val, 9.5f * pz);
                c.DrawText(pg.name, row_x + 2.0f, ty, 9.5f * pz, TC(t.muted));
                c.DrawText(val, row_x + row_w - vw - 2.0f, ty, 9.5f * pz, val_col);
                // Hairline track hint under the row.
                c.DrawLine(row_x + 2.0f, row_y + row_h - 2.0f,
                           row_x + row_w - 2.0f, row_y + row_h - 2.0f,
                           WithAlpha(t.border, 0.6f), 1.0f);
            } else {  // text/other — read-only summary, inspector edits
                std::string val = FormatValue(pv);
                if (val.size() > 16) val = val.substr(0, 15) + "…";
                c.DrawText(pg.name, row_x + 2.0f, ty, 9.0f * pz, TC(t.muted));
                c.DrawText("=", row_x + 4.0f + EstTextW(pg.name, 9.0f * pz) + 3.0f,
                           ty, 9.0f * pz, TC(t.muted));
                c.DrawText(val, row_x + 4.0f + EstTextW(pg.name, 9.0f * pz) + 12.0f,
                           ty, 9.0f * pz, TC(t.foreground));
            }
        }
        {
            // Ordered param count mirrors RebuildGeometries: spec params
            // plus stored params missing from the spec.
            size_t total_params = node.params.size();
            if (desc != nullptr) {
                std::set<std::string> covered;
                for (const auto& s : desc->params) {
                    covered.insert(s.name);
                    if (node.params.count(s.name) == 0) ++total_params;
                }
            }
            if (total_params > g.params.size()) {
                const float more_y = static_cast<float>(ToScreenY(
                    g.y + kHeaderH +
                    std::max<double>(static_cast<double>(node.inputs.size()),
                                     static_cast<double>(node.outputs.size())) *
                        kRowH +
                    kMaxParamRows * kParamRowH + 3.0));
                c.DrawText("+" + std::to_string(total_params - g.params.size()) + " more",
                           rx + 12.0f, more_y + 10.0f,
                           9.0f * static_cast<float>(zc), TC(t.muted));
            }
        }

        // Error text under the node.
        if (has_error) {
            auto eit = node_error_text_.find(id);
            if (eit != node_error_text_.end()) {
                std::string msg = eit->second;
                if (msg.size() > 40) msg = msg.substr(0, 39) + "…";
                c.DrawText(msg, rx, ry + rh + 4.0f, 10.0f, DangerC());
            }
        }
    }
}

void NodeGraphEditorWidget::DrawSelectionFrame(Canvas::Canvas& c) {
    // Marching-ants style dashed frame around the union of selected nodes —
    // makes multi-selection scope obvious before align/distribute ops.
    const auto& sel = Current().selected_nodes;
    if (sel.size() < 2) return;

    double lo_x = 1e30, hi_x = -1e30, lo_y = 1e30, hi_y = -1e30;
    for (const auto id : sel) {
        const auto it = geoms_.find(id);
        if (it == geoms_.end()) continue;
        const auto& g = it->second;
        lo_x = std::min(lo_x, g.x);
        hi_x = std::max(hi_x, g.x + g.w);
        lo_y = std::min(lo_y, g.y);
        hi_y = std::max(hi_y, g.y + g.h);
    }
    if (hi_x <= lo_x || hi_y <= lo_y) return;

    const float pad = 8.0f;
    const float x0 = static_cast<float>(ToScreenX(lo_x)) - pad;
    const float y0 = static_cast<float>(ToScreenY(lo_y)) - pad;
    const float x1 = static_cast<float>(ToScreenX(hi_x)) + pad;
    const float y1 = static_cast<float>(ToScreenY(hi_y)) + pad;
    if (x1 < -40.0f || x0 > canvas_w_ + 40.0f ||
        y1 < -40.0f || y0 > canvas_h_ + 40.0f) {
        return;
    }

    const auto col = AccentC(0.8f);
    constexpr float kDash = 6.0f, kGap = 4.0f;
    auto dashed = [&](float ax, float ay, float bx, float by) {
        const float len = std::hypot(bx - ax, by - ay);
        if (len <= 0.0f) return;
        const float ux = (bx - ax) / len, uy = (by - ay) / len;
        for (float d = 0.0f; d < len; d += kDash + kGap) {
            const float e = std::min(d + kDash, len);
            c.DrawLine(ax + ux * d, ay + uy * d, ax + ux * e, ay + uy * e,
                       col, 1.4f);
        }
    };
    dashed(x0, y0, x1, y0);
    dashed(x1, y0, x1, y1);
    dashed(x1, y1, x0, y1);
    dashed(x0, y1, x0, y0);
}

void NodeGraphEditorWidget::DrawBoxSelect(Canvas::Canvas& c) {
    if (!box_active_) return;
    const float x = static_cast<float>(std::min(static_cast<double>(box_rect_.x), mouse_sx_));
    const float y = static_cast<float>(std::min(static_cast<double>(box_rect_.y), mouse_sy_));
    const float w = static_cast<float>(std::abs(mouse_sx_ - box_rect_.x));
    const float h = static_cast<float>(std::abs(mouse_sy_ - box_rect_.y));
    c.FillRect({x, y, w, h}, AccentC(0.10f));
    c.StrokeRect({x, y, w, h}, AccentC(0.75f), 1.0f);
}

void NodeGraphEditorWidget::DrawMinimap(Canvas::Canvas& c) {
    if (geoms_.empty() || canvas_w_ < 420 || canvas_h_ < 260) return;
    const double bw = world_max_x_ - world_min_x_ + 40.0;
    const double bh = world_max_y_ - world_min_y_ + 40.0;
    const double mw = 170.0, mh = std::min(110.0, mw * bh / bw);
    const float mx = static_cast<float>(canvas_w_ - mw - 12.0);
    const float my = static_cast<float>(canvas_h_ - mh - 12.0);
    const double s = std::min(mw / bw, mh / bh);
    const double ox = world_min_x_ - 20.0, oy = world_min_y_ - 20.0;

    const auto& t = CT();
    DrawPopoverChrome(c, mx, my, static_cast<float>(mw), static_cast<float>(mh));
    for (const auto& [id, g] : geoms_) {
        c.FillRect({static_cast<float>(mx + (g.x - ox) * s),
                    static_cast<float>(my + (g.y - oy) * s),
                    static_cast<float>(g.w * s), static_cast<float>(g.h * s)},
                   Current().selected_nodes.count(id)
                       ? AccentC(0.9f)
                       : WithAlpha(Mix(t.muted, t.foreground, 0.3f), 0.75f),
                   1.5f);
    }
    // Viewport rect.
    const double vx0 = ToWorldX(0.0) - ox, vy0 = ToWorldY(0.0) - oy;
    const double vx1 = ToWorldX(canvas_w_) - ox, vy1 = ToWorldY(canvas_h_) - oy;
    c.StrokeRect({static_cast<float>(mx + vx0 * s), static_cast<float>(my + vy0 * s),
                  static_cast<float>((vx1 - vx0) * s), static_cast<float>((vy1 - vy0) * s)},
                 AccentC(0.9f), 1.2f);
}

// ── Palette / menu / rename / toast drawing ────────────────────────────────

// shadcn popover metrics (canvas px).
namespace {
constexpr float kMenuPad = 6.0f;
constexpr float kMenuItemH = 30.0f;
constexpr float kMenuSepH = 9.0f;
constexpr float kMenuRadius = 8.0f;
constexpr float kMenuMinW = 190.0f;
constexpr float kMenuMaxW = 320.0f;
// Palette popover metrics — shared by draw + hit-test code.
constexpr float kPaletteW = 340.0f;
constexpr float kPaletteRowH = 30.0f;
constexpr float kPaletteHeaderH = 40.0f;
constexpr int kPaletteVisible = 14;
constexpr float kPaletteBottomPad = 10.0f;
constexpr float kScrollbarW = 10.0f;
}

float NodeGraphEditorWidget::MenuContentHeight() const {
    float h = kMenuPad * 2.0f;
    for (const auto& e : menu_entries_) {
        h += e.separator ? kMenuSepH : kMenuItemH;
    }
    return h;
}

float NodeGraphEditorWidget::MenuScrollMax() const {
    const MenuBox b = ComputeMenuBox();
    return std::max(0.0f, MenuContentHeight() - (b.h - kMenuPad * 2.0f));
}

float NodeGraphEditorWidget::PaletteScrollMax() const {
    const float content =
        static_cast<float>(palette_items_.size()) * kPaletteRowH;
    return std::max(0.0f, content - kPaletteVisible * kPaletteRowH);
}

NodeGraphEditorWidget::MenuBox NodeGraphEditorWidget::ComputeMenuBox() const {
    MenuBox b;
    float content_w = 0.0f;
    for (const auto& e : menu_entries_) {
        if (e.separator) {
            continue;
        }
        const float lw = EstTextW(e.label, 12.5f) + 20.0f;
        const float sw = e.shortcut.empty() ? 0.0f
                                            : EstTextW(e.shortcut, 10.5f) + 18.0f;
        content_w = std::max(content_w, lw + sw);
    }
    b.w = std::clamp(content_w, kMenuMinW, kMenuMaxW);
    // Cap the popover at the canvas height (minus margin); overflow scrolls.
    b.h = std::min(MenuContentHeight(),
                   std::max(96.0f, static_cast<float>(canvas_h_ - 8.0)));
    b.x = static_cast<float>(std::clamp(menu_x_, 4.0,
                                        static_cast<double>(canvas_w_) - b.w - 4.0));
    b.y = static_cast<float>(std::clamp(menu_y_, 4.0,
                                        static_cast<double>(canvas_h_) - b.h - 4.0));
    return b;
}

void NodeGraphEditorWidget::MenuEntryRect(int i, float& top, float& h) const {
    float y = kMenuPad;
    for (int k = 0; k < i && k < static_cast<int>(menu_entries_.size()); ++k) {
        y += menu_entries_[k].separator ? kMenuSepH : kMenuItemH;
    }
    top = y;
    h = (i >= 0 && i < static_cast<int>(menu_entries_.size()) &&
         menu_entries_[i].separator)
            ? kMenuSepH
            : kMenuItemH;
}

int NodeGraphEditorWidget::MenuIndexAtY(double y) const {
    const MenuBox b = ComputeMenuBox();
    if (y < b.y || y > b.y + b.h) return -1;
    // Pointer y maps into unscrolled content space by adding the scroll
    // offset; MenuEntryRect tops are content-space (kMenuPad-based).
    const double cy = y - b.y + menu_scroll_;
    for (size_t i = 0; i < menu_entries_.size(); ++i) {
        float top = 0.0f, h = 0.0f;
        MenuEntryRect(static_cast<int>(i), top, h);
        if (cy >= top && cy < top + h) {
            return menu_entries_[i].separator ? -1
                                              : static_cast<int>(i);
        }
    }
    return -1;
}

void NodeGraphEditorWidget::EnsureMenuItemVisible(int index) {
    if (index < 0 || index >= static_cast<int>(menu_entries_.size())) return;
    float top = 0.0f, h = 0.0f;
    MenuEntryRect(index, top, h);
    const MenuBox b = ComputeMenuBox();
    const float viewport = b.h - kMenuPad * 2.0f;
    // Content window currently shown: [scroll, scroll + viewport] offset by
    // the pad; entry tops already include kMenuPad.
    if (top - kMenuPad < menu_scroll_) {
        menu_scroll_ = std::max(0.0f, top - kMenuPad);
    } else if (top + h - kMenuPad > menu_scroll_ + viewport) {
        menu_scroll_ = std::min(MenuScrollMax(),
                                top + h - kMenuPad - viewport);
    }
    menu_scroll_ = std::clamp(menu_scroll_, 0.0f, MenuScrollMax());
}

void NodeGraphEditorWidget::EnsurePaletteItemVisible(int index) {
    if (index < 0) return;
    const float row_top = static_cast<float>(index) * kPaletteRowH;
    const float viewport = kPaletteVisible * kPaletteRowH;
    if (row_top < palette_scroll_) {
        palette_scroll_ = row_top;
    } else if (row_top + kPaletteRowH > palette_scroll_ + viewport) {
        palette_scroll_ = row_top + kPaletteRowH - viewport;
    }
    palette_scroll_ = std::clamp(palette_scroll_, 0.0f, PaletteScrollMax());
}

// Draws the shadcn-style popover chrome shared by menu + palette:
// soft shadow, surface bg, hairline border, 8px radius.
void NodeGraphEditorWidget::DrawPopoverChrome(Canvas::Canvas& c, float x,
                                              float y, float w, float h,
                                              float alpha) {
    const auto& t = CT();
    const bool dark = t.background.r + t.background.g + t.background.b < 1.5f;
    const auto FA = [alpha](Canvas::Canvas::Color col) {
        col.a *= alpha;
        return col;
    };
    // Layered fake shadow (no blur primitive on Canvas).
    const float shadow_a = (dark ? 0.28f : 0.10f) * alpha;
    for (int i = 3; i >= 1; --i) {
        const float grow = static_cast<float>(i) * 2.0f;
        c.FillRect({x - grow, y - grow * 0.5f + 2.0f, w + grow * 2.0f,
                    h + grow * 2.0f},
                   {0.0f, 0.0f, 0.0f, shadow_a * (4 - i) / 6.0f},
                   kMenuRadius + grow * 0.5f);
    }
    c.FillRect({x, y, w, h}, FA(PopoverBg()), kMenuRadius);
    c.StrokeRect({x, y, w, h}, FA(WithAlpha(t.border, 0.9f)), 1.0f,
                 kMenuRadius);
}

void NodeGraphEditorWidget::RebuildPaletteItems() {
    palette_items_.clear();
    palette_titles_.clear();
    palette_cats_.clear();
    palette_types_.clear();
    const std::string f = XER::Helper::String::ToLowerAscii(palette_filter_);
    auto& reg = NG::NodeTypeRegistry::Shared();
    for (const auto& type : reg.Types()) {
        const auto* d = reg.Find(type);
        if (d == nullptr) continue;
        std::string hay = XER::Helper::String::ToLowerAscii(type + " " + d->title + " " + d->category);
        if (!f.empty() && hay.find(f) == std::string::npos) continue;
        palette_types_.push_back(type);
        palette_titles_.push_back(d->title);
        palette_cats_.push_back(d->category);
        palette_items_.push_back(d->title + "  ·  " + d->category);
        if (palette_items_.size() >= 200) break;
    }
    palette_scroll_ = 0.0f;
    palette_hover_ = palette_items_.empty() ? -1 : 0;
}

void NodeGraphEditorWidget::DrawPalette(Canvas::Canvas& c) {
    if (!palette_open_) return;
    const auto& t = CT();
    const int count = static_cast<int>(palette_items_.size());
    const float h = kPaletteHeaderH +
                    std::min(count, kPaletteVisible) * kPaletteRowH +
                    kPaletteBottomPad;
    float px = static_cast<float>(palette_anchor_x_);
    float py = static_cast<float>(palette_anchor_y_);
    px = std::clamp(px, 8.0f, static_cast<float>(canvas_w_) - kPaletteW - 8.0f);
    py = std::clamp(py, 8.0f, static_cast<float>(canvas_h_) - h - 8.0f);
    palette_scroll_ = std::clamp(palette_scroll_, 0.0f, PaletteScrollMax());

    // Entrance: fade + 6px rise from the anchor point.
    const float p = palette_anim_.Value();
    py += (1.0f - p) * -6.0f;
    const auto FA = [p](Canvas::Canvas::Color col) {
        col.a *= p;
        return col;
    };

    DrawPopoverChrome(c, px, py, kPaletteW, h, p);
    // Header: label + live filter text.
    c.DrawText("Add node", px + 14.0f, py + 17.0f, 12.5f, FA(TC(t.foreground)));
    c.DrawText(palette_filter_.empty() ? "type to filter…" : palette_filter_,
               px + 14.0f, py + 33.0f, 11.0f, FA(TC(t.muted)));
    c.DrawLine(px + 10.0f, py + kPaletteHeaderH - 1.0f, px + kPaletteW - 10.0f,
               py + kPaletteHeaderH - 1.0f, FA(WithAlpha(t.border, 0.8f)), 1.0f);

    // Scrollable item list, clipped to the rows band.
    const float list_top = py + kPaletteHeaderH;
    const float list_h = h - kPaletteHeaderH - 4.0f;
    c.PushClip({px + 1.0f, list_top, kPaletteW - 2.0f, list_h});
    const int first = std::max(0, static_cast<int>(palette_scroll_ / kPaletteRowH));
    const int last = std::min(count, first + kPaletteVisible + 1);
    for (int i = first; i < last; ++i) {
        const float ry = list_top + i * kPaletteRowH - palette_scroll_;
        if (i == palette_hover_) {
            c.FillRect({px + 5.0f, ry + 2.0f, kPaletteW - 10.0f,
                        kPaletteRowH - 4.0f},
                       FA(WithAlpha(t.hover, 0.85f)), 6.0f);
        }
        const std::string title = i < static_cast<int>(palette_titles_.size())
                                      ? palette_titles_[i]
                                      : palette_items_[i];
        const std::string cat = i < static_cast<int>(palette_cats_.size())
                                    ? palette_cats_[i]
                                    : std::string{};
        c.DrawText(title, px + 14.0f, ry + 20.0f, 12.0f, FA(TC(t.foreground)));
        if (!cat.empty()) {
            const float cw = EstTextW(cat, 10.5f);
            c.DrawText(cat, px + kPaletteW - 14.0f - cw, ry + 20.0f, 10.5f,
                       FA(TC(t.muted)));
        }
    }
    c.PopClip();

    // Scrollbar thumb when the list overflows.
    const float smax = PaletteScrollMax();
    if (smax > 0.0f) {
        const float track_h = list_h;
        float thumb_h = track_h * (list_h / std::max(1.0f,
                           static_cast<float>(count) * kPaletteRowH));
        thumb_h = std::clamp(thumb_h, 22.0f, track_h);
        const float travel = track_h - thumb_h;
        const float ty = list_top + (palette_scroll_ / smax) * travel;
        c.FillRect({px + kPaletteW - 7.0f, ty, 4.0f, thumb_h},
                   FA(WithAlpha(t.muted, 0.5f)), 2.0f);
    }
    if (count == 0) {
        c.DrawText("no matching node types", px + 14.0f,
                   py + kPaletteHeaderH + 20.0f, 11.0f, FA(TC(t.muted)));
    }
}

void NodeGraphEditorWidget::DrawMenu(Canvas::Canvas& c) {
    if (!menu_open_ || menu_entries_.empty()) return;
    const auto& t = CT();
    // Entrance: short ease-out fade + 8px rise (exits are instant).
    const float p = menu_anim_.Value();
    MenuBox b = ComputeMenuBox();
    b.y += (1.0f - p) * -8.0f;
    const auto FA = [p](Canvas::Canvas::Color col) {
        col.a *= p;
        return col;
    };
    menu_scroll_ = std::clamp(menu_scroll_, 0.0f, MenuScrollMax());
    DrawPopoverChrome(c, b.x, b.y, b.w, b.h, p);

    // Rows clipped to the inner band; overflow scrolls via menu_scroll_.
    const float view_h = b.h - kMenuPad * 2.0f;
    c.PushClip({b.x + 1.0f, b.y + kMenuPad, b.w - 2.0f, view_h});
    for (size_t i = 0; i < menu_entries_.size(); ++i) {
        const auto& e = menu_entries_[i];
        float top = 0.0f, rh = 0.0f;
        MenuEntryRect(static_cast<int>(i), top, rh);
        const float ry = b.y + top - menu_scroll_;
        if (ry + rh < b.y + kMenuPad || ry > b.y + b.h - kMenuPad) {
            continue; // fully scrolled out — skip draw calls entirely
        }
        if (e.separator) {
            c.DrawLine(b.x + 10.0f, ry + rh * 0.5f, b.x + b.w - 10.0f,
                       ry + rh * 0.5f, FA(WithAlpha(t.border, 0.9f)), 1.0f);
            continue;
        }
        const bool hot = static_cast<int>(i) == menu_hover_ && !e.disabled;
        if (hot) {
            // shadcn: destructive items get a red-tinted hover.
            c.FillRect({b.x + 4.0f, ry + 2.0f, b.w - 8.0f, rh - 4.0f},
                       FA(e.danger ? DangerC(0.14f)
                                   : WithAlpha(t.hover, 0.9f)),
                       6.0f);
        }
        Canvas::Canvas::Color lc = FA(e.danger ? DangerC() : TC(t.foreground));
        if (e.disabled) lc.a *= 0.38f;
        c.DrawText(e.label, b.x + 12.0f, ry + rh * 0.5f + 4.5f, 12.0f, lc);
        if (!e.shortcut.empty()) {
            Canvas::Canvas::Color sc = FA(TC(t.muted));
            if (e.disabled) sc.a *= 0.38f;
            const float sw = EstTextW(e.shortcut, 10.5f);
            c.DrawText(e.shortcut, b.x + b.w - 12.0f - sw,
                       ry + rh * 0.5f + 4.0f, 10.5f, sc);
        }
    }
    c.PopClip();

    // Scrollbar thumb when the menu overflows.
    const float smax = MenuScrollMax();
    if (smax > 0.0f) {
        const float track_h = view_h;
        float thumb_h = track_h * (view_h / MenuContentHeight());
        thumb_h = std::clamp(thumb_h, 20.0f, track_h);
        const float travel = track_h - thumb_h;
        const float ty = b.y + kMenuPad + (menu_scroll_ / smax) * travel;
        c.FillRect({b.x + b.w - 7.0f, ty, 4.0f, thumb_h},
                   FA(WithAlpha(t.muted, 0.5f)), 2.0f);
    }
}

void NodeGraphEditorWidget::DrawRenameField(Canvas::Canvas& c) {
    if (!renaming_) return;
    auto git = geoms_.find(rename_node_);
    if (git == geoms_.end()) return;
    const NodeGeom& g = git->second;
    const float rx = static_cast<float>(ToScreenX(g.x));
    const float ry = static_cast<float>(ToScreenY(g.y));
    const float rw = static_cast<float>(g.w * Current().zoom);
    const float rh = static_cast<float>(kHeaderH * Current().zoom);
    const auto& t = CT();
    c.FillRect({rx + 6.0f, ry + 4.0f, rw - 12.0f, rh - 8.0f},
               TC(Mix(t.background, t.surface, 0.5f)), 4.0f);
    c.StrokeRect({rx + 6.0f, ry + 4.0f, rw - 12.0f, rh - 8.0f},
                 AccentC(1.0f), 1.2f, 4.0f);
    c.DrawText(rename_buffer_ + "_", rx + 12.0f, ry + 4.0f + rh * 0.62f,
               12.0f, TC(t.foreground));
}

void NodeGraphEditorWidget::DrawToast(Canvas::Canvas& c) {
    if (toast_text_.empty()) return;
    const auto now = std::chrono::steady_clock::now();
    if (now > toast_until_) {
        toast_text_.clear();
        return;
    }
    // Count UTF-8 code points, not bytes — Cyrillic is 2 bytes/char and a
    // byte count would make the toast narrower than its text.
    std::size_t cps = 0;
    for (const unsigned char ch : toast_text_) {
        if ((ch & 0xC0) != 0x80) ++cps;
    }
    const float w = static_cast<float>(cps) * 7.0f + 30.0f;
    const float x = (static_cast<float>(canvas_w_) - w) * 0.5f;
    // Entrance: fade + 10px drop from above.
    const float p = toast_anim_.Value();
    const float ty = 10.0f - (1.0f - p) * 10.0f;
    const auto FA = [p](Canvas::Canvas::Color col) {
        col.a *= p;
        return col;
    };
    // shadcn toast: elevated surface + border, semantic accent edge.
    c.FillRect({x - 2.0f, ty - 2.0f, w + 4.0f, 34.0f},
               FA({0.0f, 0.0f, 0.0f, CT().background.r + CT().background.g +
                                      CT().background.b < 1.5f ? 0.30f : 0.12f}),
               9.0f);
    c.FillRect({x, ty, w, 30.0f}, FA(PopoverBg()), 7.0f);
    c.FillRect({x, ty, 3.0f, 30.0f},
               FA(toast_is_error_
                      ? DangerC()
                      : Canvas::Canvas::Color{0.13f, 0.77f, 0.37f, 1.0f}),
               1.5f);
    c.DrawText(toast_text_, x + 14.0f, ty + 20.0f, 12.0f,
               FA(TC(CT().foreground)));
}

void NodeGraphEditorWidget::DrawChromeOverlay(Canvas::Canvas& c) {
    // Breadcrumb path + zoom, top-left inside the canvas (replaces the old
    // toolbar labels).
    std::string path;
    for (const auto& l : levels_) {
        if (!path.empty()) path += "  /  ";
        path += l.crumb;
    }
    const std::string zoom = std::to_string(static_cast<int>(Current().zoom * 100.0)) + "%";
    const auto& t = CT();
    c.DrawText(path + "   ·   " + zoom, 12.0f, 16.0f, 11.0f, TC(t.muted));
    // Interaction hints, bottom-left.
    c.DrawText("dbl-click: add / rename   right-click: menu   Del: remove   "
               "wheel: zoom   mid-drag: pan",
               12.0f, static_cast<float>(canvas_h_) - 10.0f, 10.0f,
               TC(t.muted));
}

// ── Hit testing ────────────────────────────────────────────────────────────

NodeGraph::NodeId NodeGraphEditorWidget::HitNode(double sx, double sy) const {
    const double wx = ToWorldX(sx), wy = ToWorldY(sy);
    for (auto it = geoms_.rbegin(); it != geoms_.rend(); ++it) {
        const NodeGeom& g = it->second;
        if (wx >= g.x && wx <= g.x + g.w && wy >= g.y && wy <= g.y + g.h) {
            return it->first;
        }
    }
    return NG::kInvalidNode;
}

const NodeGraphEditorWidget::PortGeom* NodeGraphEditorWidget::HitPort(
    NodeGraph::NodeId node, double sx, double sy, bool want_output) const {
    auto git = geoms_.find(node);
    if (git == geoms_.end()) return nullptr;
    const NodeGeom& g = git->second;
    for (const auto& p : g.ports) {
        if (p.is_output != want_output) continue;
        const double px = ToScreenX(want_output ? g.x + g.w : g.x);
        const double py = ToScreenY(p.y);
        if (std::abs(py - sy) < 10.0 && std::abs(px - sx) < 14.0) {
            return &p;
        }
    }
    return nullptr;
}

NodeGraph::LinkId NodeGraphEditorWidget::HitLink(double sx, double sy) const {
    // Cheap bbox reject first — the cached curve bbox already includes
    // the control points, so only links near the cursor get sampled.
    for (const auto& [id, lg] : link_geoms_) {
        if (sx < lg.minx || sx > lg.maxx || sy < lg.miny || sy > lg.maxy) {
            continue;
        }
        // Sample the cubic bezier (12 steps is plenty at UI scale).
        double best = 1e30;
        for (int i = 0; i <= 12; ++i) {
            const double t = i / 12.0;
            const double u = 1.0 - t;
            const double bx = u*u*u*lg.x1 + 3*u*u*t*lg.cx1 +
                              3*u*t*t*lg.cx2 + t*t*t*lg.x2;
            const double by = u*u*u*lg.y1 + 3*u*u*t*lg.cy1 +
                              3*u*t*t*lg.cy2 + t*t*t*lg.y2;
            const double d2 = (bx - sx) * (bx - sx) + (by - sy) * (by - sy);
            best = std::min(best, d2);
            if (best < 4.0) break;  // well inside — no need to finish
        }
        if (best < 49.0) return id;
    }
    return NG::kInvalidLink;
}

NodeGraph::LinkId NodeGraphEditorWidget::HitLinkEndpoint(
    double sx, double sy, bool* grabbed_output) const {
    if (grabbed_output != nullptr) *grabbed_output = false;
    constexpr double kGrab = 14.0;
    const auto& links = Current().graph->links();
    for (const auto& [id, lg] : link_geoms_) {
        const auto it = links.find(id);
        if (it == links.end()) continue;
        if (std::hypot(sx - lg.x1, sy - lg.y1) <= kGrab) {
            if (grabbed_output != nullptr) *grabbed_output = true;
            return id;
        }
        if (std::hypot(sx - lg.x2, sy - lg.y2) <= kGrab) {
            return id;  // grabbed_output stays false -> "to" end
        }
    }
    return NG::kInvalidLink;
}

const NodeGraphEditorWidget::ParamGeom* NodeGraphEditorWidget::HitParam(
    NodeGraph::NodeId node, double sx, double sy) const {
    auto git = geoms_.find(node);
    if (git == geoms_.end()) return nullptr;
    const NodeGeom& g = git->second;
    for (const auto& pg : g.params) {
        const double y0 = ToScreenY(pg.y);
        const double y1 = ToScreenY(pg.y + pg.h);
        const double x0 = ToScreenX(g.x);
        const double x1 = ToScreenX(g.x + g.w);
        if (sx >= x0 && sx <= x1 && sy >= y0 && sy <= y1) {
            return &pg;
        }
    }
    return nullptr;
}

NodeGraph::PortRef NodeGraphEditorWidget::ResolveDropPort(double sx, double sy,
                                                        bool want_output) const {
    double best_d2 = 100.0;  // 10px radius
    NG::PortRef best;
    const double margin = 14.0;  // hit radius + stroke slack, screen px
    for (const auto& [nid, g] : geoms_) {
        // Node-level reject: ports sit on the node's left/right edges
        // inside its vertical span, so a cursor outside the padded rect
        // cannot match any of them — skip the port loop entirely.
        if (sx < ToScreenX(g.x) - margin || sx > ToScreenX(g.x + g.w) + margin ||
            sy < ToScreenY(g.y) - margin || sy > ToScreenY(g.y + g.h) + margin) {
            continue;
        }
        for (const auto& p : g.ports) {
            if (p.is_output != want_output) continue;
            const double px = ToScreenX(want_output ? g.x + g.w : g.x);
            const double py = ToScreenY(p.y);
            const double d2 = (px - sx) * (px - sx) + (py - sy) * (py - sy);
            if (d2 < best_d2) {
                best_d2 = d2;
                best = {nid, p.name};
            }
        }
    }
    return best;
}

// ── Input ──────────────────────────────────────────────────────────────────

void NodeGraphEditorWidget::OnCanvasInput(unsigned msg, int x, int y, int button,
                                          int wheel, int key, int mods) {
    mouse_sx_ = x;
    mouse_sy_ = y;
    // Canvas input bypasses the widget tree, so a modal overlay (chrome
    // context menu, select dropdown) would never see this click and stay
    // stuck open. Dismiss it on any canvas press.
    if ((msg == WM_LBUTTONDOWN || msg == WM_MBUTTONDOWN || msg == WM_RBUTTONDOWN ||
         msg == WM_MOUSEWHEEL) &&
        GetTree() != nullptr && GetTree()->GetOverlay() != nullptr) {
        GetTree()->HideOverlay();
    }
    switch (msg) {
    case WM_MOUSEWHEEL: HandleWheel(x, y, wheel); break;
    case WM_MOUSEMOVE:  HandleMouseMove(x, y, button, mods); break;
    case WM_LBUTTONDOWN:
        HandleMouseDown(x, y, 0, mods, false); break;
    case WM_LBUTTONDBLCLK:
        HandleMouseDown(x, y, 0, mods, true); break;
    case WM_LBUTTONUP:
        HandleMouseUp(x, y, 0, mods); break;
    case WM_MBUTTONDOWN:
        drag_mode_ = DragMode::Pan;
        drag_start_sx_ = drag_last_sx_ = x;
        drag_start_sy_ = drag_last_sy_ = y;
        break;
    case WM_MBUTTONUP:
        if (drag_mode_ == DragMode::Pan) drag_mode_ = DragMode::None;
        break;
    case WM_RBUTTONDOWN: {
        if (palette_open_ || menu_open_) { ClosePalette(); CloseMenu(); break; }
        if (renaming_) { CommitRename(); break; }
        const auto nid = HitNode(x, y);
        if (nid != NG::kInvalidNode) {
            if (Current().selected_nodes.count(nid) == 0) {
                Current().selected_nodes = {nid};
                Current().selected_link.reset();
            }
            OpenNodeMenu(nid, x, y);
        } else {
            const auto lid = HitLink(x, y);
            if (lid != NG::kInvalidLink) {
                Current().selected_link = lid;
                Current().selected_nodes.clear();
                OpenLinkMenu(lid, x, y);
            } else {
                Current().selected_nodes.clear();
                Current().selected_link.reset();
                OpenCanvasMenu(x, y);
            }
        }
        break;
    }
    case WM_KEYDOWN:
        HandleKey(msg, key, mods);
        break;
    case WM_CHAR:
        HandleChar(key);
        break;
    default:
        break;
    }
}

void NodeGraphEditorWidget::HandleWheel(int x, int y, int wheel) {
    (void)x; (void)y;
    // Popovers consume the wheel first: palette rows, then menu entries,
    // plain canvas zooms.
    if (palette_open_ && PaletteScrollMax() > 0.0f) {
        palette_scroll_ = std::clamp(
            palette_scroll_ - static_cast<float>(wheel) / 120.0f *
                kPaletteRowH * 3.0f,
            0.0f, PaletteScrollMax());
        canvas_.Invalidate();
        return;
    }
    if (menu_open_ && MenuScrollMax() > 0.0f) {
        menu_scroll_ = std::clamp(
            menu_scroll_ - static_cast<float>(wheel) / 120.0f * 96.0f,
            0.0f, MenuScrollMax());
        canvas_.Invalidate();
        return;
    }
    const double factor = wheel > 0 ? 1.12 : 0.89;
    const double nz = std::clamp(Current().zoom * factor, 0.2, 3.0);
    const double wx = ToWorldX(x), wy = ToWorldY(y);
    Current().zoom = nz;
    Current().pan_x = x - wx * nz;
    Current().pan_y = y - wy * nz;
    canvas_.Invalidate();
}

void NodeGraphEditorWidget::HandleMouseDown(int x, int y, int button, int mods, bool dblclk) {
    (void)button;
    // Palette intercepts all clicks while open.
    if (palette_open_) {
        const int count = static_cast<int>(palette_items_.size());
        const float h = kPaletteHeaderH +
                        std::min(count, kPaletteVisible) * kPaletteRowH +
                        kPaletteBottomPad;
        const float px = std::clamp(static_cast<float>(palette_anchor_x_), 8.0f,
                                    static_cast<float>(canvas_w_) - kPaletteW - 8.0f);
        const float py = std::clamp(static_cast<float>(palette_anchor_y_), 8.0f,
                                    static_cast<float>(canvas_h_) - h - 8.0f);
        if (x >= px && x <= px + kPaletteW && y >= py && y <= py + h) {
            if (y >= py + kPaletteHeaderH) {
                const int idx = static_cast<int>(std::floor(
                    (y - py - kPaletteHeaderH + palette_scroll_) / kPaletteRowH));
                if (idx >= 0 && idx < count) {
                    AddNodeAtPalette(idx);
                    return;
                }
            }
            ClosePalette();
            return;
        }
        ClosePalette();
        // fall through: click also acts on canvas
    }
    if (menu_open_) {
        const MenuBox b = ComputeMenuBox();
        if (x >= b.x && x <= b.x + b.w && y >= b.y && y <= b.y + b.h) {
            const int idx = MenuIndexAtY(y);
            if (idx >= 0) ActivateMenuEntry(idx);
            return;
        }
        CloseMenu();
    }
    if (renaming_) {
        CommitRename();
        return;
    }

    if (dblclk) {
        const auto nid = HitNode(x, y);
        if (nid != NG::kInvalidNode) {
            const NG::Node* n = Current().graph->FindNode(nid);
            if (n != nullptr && n->type == "graph.subgraph" && n->subgraph) {
                EnterSubgraph(nid);
            } else {
                StartRename(nid);
            }
        } else {
            OpenPalette(x, y);
        }
        return;
    }

    // Port grab -> start link drag (either direction).
    const auto port_node = HitNode(x, y);
    if (port_node != NG::kInvalidNode) {
        if (const PortGeom* p = HitPort(port_node, x, y, true)) {
            link_from_ = {port_node, p->name};
            link_from_is_output_ = true;
            link_dragging_ = true;
            drop_valid_ = false;
            Current().selected_link.reset();
            canvas_.Invalidate();
            return;
        }
        if (const PortGeom* p = HitPort(port_node, x, y, false)) {
            link_from_ = {port_node, p->name};
            link_from_is_output_ = false;
            link_dragging_ = true;
            drop_valid_ = false;
            // Select existing link on this input for visibility.
            if (const NG::Link* l = Current().graph->FindLinkTo(link_from_)) {
                Current().selected_link = l->id;
            }
            canvas_.Invalidate();
            return;
        }
    }

    const auto nid = HitNode(x, y);
    if (nid != NG::kInvalidNode) {
        auto& sel = Current().selected_nodes;
        if (mods & 1) {  // Shift toggles.
            if (sel.count(nid)) sel.erase(nid); else sel.insert(nid);
        } else if (sel.count(nid) == 0) {
            sel = {nid};
            Current().selected_link.reset();
        }
        // In-node parameter controls sit below the port rows — they get
        // the press before the node body starts a drag.
        if (const ParamGeom* pg = HitParam(nid, x, y)) {
            NG::Node* pnode = Current().graph->FindNode(nid);
            if (pnode != nullptr) {
                const NG::NodeTypeDescriptor* pdesc =
                    Current().graph->registry().Find(pnode->type);
                auto spec_of = [&](const std::string& pname)
                    -> const NG::ParamSpec* {
                    if (pdesc == nullptr) return nullptr;
                    for (const auto& s : pdesc->params) {
                        if (s.name == pname) return &s;
                    }
                    return nullptr;
                };
                auto value_of = [&](const std::string& pname) -> NG::Value {
                    const auto it = pnode->params.find(pname);
                    if (it != pnode->params.end()) return it->second;
                    if (const NG::ParamSpec* s = spec_of(pname)) {
                        return s->default_value;
                    }
                    return NG::Value();
                };
                if (pg->kind == 1) {  // bool — click toggles
                    const NG::Value cur = value_of(pg->name);
                    const NG::Value def =
                        spec_of(pg->name) ? spec_of(pg->name)->default_value
                                          : NG::Value();
                    const bool on = !cur.IsNone() ? cur.AsBool() : def.AsBool();
                    Current().history->SetParam(nid, pg->name, NG::Value(!on));
                    ScheduleAutosave();
                    canvas_.Invalidate();
                    return;
                }
                if (pg->kind == 2) {  // options — click cycles (shift = back)
                    const NG::ParamSpec* s = spec_of(pg->name);
                    if (s != nullptr && !s->options.empty()) {
                        const std::string cur_s = value_of(pg->name).AsString();
                        int idx = 0;
                        for (size_t i = 0; i < s->options.size(); ++i) {
                            if (s->options[i] == cur_s) {
                                idx = static_cast<int>(i);
                                break;
                            }
                        }
                        const int dir = (mods & 1) ? -1 : 1;
                        const int next = static_cast<int>(
                            (static_cast<size_t>(idx) + s->options.size() + dir) %
                            s->options.size());
                        Current().history->SetParam(
                            nid, pg->name, NG::Value(s->options[next]));
                        ScheduleAutosave();
                        canvas_.Invalidate();
                        return;
                    }
                }
                if (pg->kind == 4) {  // file — "…" zone opens the dialog
                    auto git = geoms_.find(nid);
                    if (git != geoms_.end()) {
                        const NodeGeom& g = git->second;
                        const double bx0 = ToScreenX(g.x + g.w) - 28.0;
                        const double bx1 = ToScreenX(g.x + g.w);
                        const double by0 = ToScreenY(pg->y);
                        const double by1 = ToScreenY(pg->y + pg->h);
                        if (x >= bx0 && x <= bx1 && y >= by0 && y <= by1) {
                            OpenParamFileBrowse(nid, pg->name);
                            canvas_.Invalidate();
                            return;
                        }
                    }
                    // Path text itself is not draggable — just select.
                    drag_mode_ = DragMode::None;
                    canvas_.Invalidate();
                    return;
                }
                if (pg->kind == 0) {  // number — begin drag-adjust
                    const NG::Value cur = value_of(pg->name);
                    param_node_ = nid;
                    param_name_ = pg->name;
                    param_kind_ = pg->kind;
                    param_start_x_ = x;
                    param_start_value_ =
                        !cur.IsNone() && cur.Type() == NG::ValueType::kNumber
                            ? cur.AsNumber()
                            : (spec_of(pg->name)
                                   ? spec_of(pg->name)->default_value.AsNumber()
                                   : 0.0);
                    drag_mode_ = DragMode::ParamDrag;
                    canvas_.Invalidate();
                    return;
                }
                // Text/other rows: select only, no drag.
                drag_mode_ = DragMode::None;
                canvas_.Invalidate();
                return;
            }
        }
        // Begin node drag for the whole selection.
        drag_mode_ = DragMode::NodeDrag;
        drag_dx_ = drag_dy_ = 0.0;
        press_wx_ = ToWorldX(x);
        press_wy_ = ToWorldY(y);
        drag_orig_.clear();
        for (auto sid : sel) {
            if (const NG::Node* n = Current().graph->FindNode(sid)) {
                drag_orig_[sid] = {n->x, n->y};
            }
        }
        canvas_.Invalidate();
        return;
    }

    const auto lid = HitLink(x, y);
    if (lid != NG::kInvalidLink) {
        Current().selected_link = lid;
        Current().selected_nodes.clear();
        // Pressing near a link's end-dot picks that end up: the curve
        // follows the cursor and can be dropped onto another compatible
        // port — the link is re-routed, not just selected.
        bool grabbed_output = false;
        if (HitLinkEndpoint(x, y, &grabbed_output) == lid) {
            if (const NG::Link* l = Current().graph->FindLink(lid);
                l != nullptr) {
                reroute_link_ = lid;
                link_from_ = grabbed_output ? l->from : l->to;
                link_from_is_output_ = grabbed_output;
                link_dragging_ = true;
                drop_valid_ = false;
                drag_mode_ = DragMode::LinkDrag;
            }
        }
        canvas_.Invalidate();
        return;
    }

    // Empty space: Ctrl or middle-button pans, plain drag box-selects.
    if ((mods & 2) != 0) {
        drag_mode_ = DragMode::Pan;
    } else {
        drag_mode_ = DragMode::BoxSelect;
        box_active_ = true;
        box_rect_ = Layout::Rect{static_cast<float>(x), static_cast<float>(y), 0.0f, 0.0f};
    }
    drag_start_sx_ = drag_last_sx_ = x;
    drag_start_sy_ = drag_last_sy_ = y;
}

void NodeGraphEditorWidget::HandleMouseMove(int x, int y, int button, int mods) {
    (void)button;
    if (palette_open_) {
        const int count = static_cast<int>(palette_items_.size());
        const float h = kPaletteHeaderH +
                        std::min(count, kPaletteVisible) * kPaletteRowH +
                        kPaletteBottomPad;
        const float px = std::clamp(static_cast<float>(palette_anchor_x_), 8.0f,
                                    static_cast<float>(canvas_w_) - kPaletteW - 8.0f);
        const float py = std::clamp(static_cast<float>(palette_anchor_y_), 8.0f,
                                    static_cast<float>(canvas_h_) - h - 8.0f);
        if (x >= px && x <= px + kPaletteW && y >= py + kPaletteHeaderH &&
            y <= py + h) {
            const int idx = static_cast<int>(std::floor(
                (y - py - kPaletteHeaderH + palette_scroll_) / kPaletteRowH));
            if (idx >= 0 && idx < count && idx != palette_hover_) {
                palette_hover_ = idx;
                canvas_.Invalidate();
            }
        }
        return;
    }
    if (menu_open_) {
        const MenuBox b = ComputeMenuBox();
        int hov = -1;
        if (x >= b.x && x <= b.x + b.w && y >= b.y && y <= b.y + b.h) {
            const int idx = MenuIndexAtY(y);
            if (idx >= 0 && idx < static_cast<int>(menu_entries_.size()) &&
                NextMenuSelectable(idx, 0) == idx) {
                hov = idx;
            }
        }
        if (hov != menu_hover_) {
            menu_hover_ = hov;
            canvas_.Invalidate();
        }
        return;
    }

    switch (drag_mode_) {
    case DragMode::Pan: {
        Current().pan_x += x - drag_last_sx_;
        Current().pan_y += y - drag_last_sy_;
        drag_last_sx_ = x;
        drag_last_sy_ = y;
        canvas_.Invalidate();
        break;
    }
    case DragMode::NodeDrag: {
        const double wx = ToWorldX(x), wy = ToWorldY(y);
        drag_dx_ = wx - press_wx_;
        drag_dy_ = wy - press_wy_;
        // Snapped offsets: each node lands on the grid, so offsets are
        // per-node (orig positions differ mod the snap step).
        for (auto [id, orig] : drag_orig_) {
            const double nx = SnapWorld(orig.first + drag_dx_);
            const double ny = SnapWorld(orig.second + drag_dy_);
            drag_offset_[id] = {nx - orig.first, ny - orig.second};
        }
        canvas_.Invalidate();
        break;
    }
    case DragMode::BoxSelect: {
        box_rect_.width = static_cast<float>(x - box_rect_.x);
        box_rect_.height = static_cast<float>(y - box_rect_.y);
        canvas_.Invalidate();
        break;
    }
    case DragMode::LinkDrag: {
        // Resolve the drop target once per mousemove; DrawTempLink reuses
        // the cached result so a painted frame never scans all ports.
        drop_port_ = ResolveDropPort(x, y, !link_from_is_output_);
        drop_valid_ = true;
        canvas_.Invalidate();
        break;
    }
    case DragMode::ParamDrag: {
        if (param_kind_ == 0 && param_node_ != NG::kInvalidNode) {
            // Drag right = increase. Sensitivity scales with |value| so
            // both 0..1 gains and 440 Hz frequencies feel right; Shift
            // gives 10x fine control. SetParamCommand::MergeWith folds the
            // whole drag into a single undo step.
            const double scale = std::max(1.0, std::fabs(param_start_value_));
            const double fine = (mods & 1) ? 0.1 : 1.0;
            const double v = param_start_value_ +
                (x - param_start_x_) * 0.02 * scale * fine;
            NG::Node* pnode = Current().graph->FindNode(param_node_);
            if (pnode != nullptr) {
                const auto it = pnode->params.find(param_name_);
                const double cur =
                    it != pnode->params.end() ? it->second.AsNumber() : v;
                if (std::abs(v - cur) > 1e-9) {
                    Current().history->SetParam(param_node_, param_name_,
                                                NG::Value(v));
                    ScheduleAutosave();
                }
            }
            canvas_.Invalidate();
        }
        break;
    }
    case DragMode::None: {
        // Node hover highlight (border brightens under the pointer).
        const auto hov = HitNode(x, y);
        if (hov != hovered_node_) {
            hovered_node_ = hov;
            canvas_.Invalidate();
        }
        // Link hover highlight — curves brighten under the cursor so the
        // endpoint grab zones are discoverable.
        const auto hl = HitLink(x, y);
        if (hl != hovered_link_) {
            hovered_link_ = hl;
            canvas_.Invalidate();
        }
        break;
    }
    }
}

void NodeGraphEditorWidget::HandleMouseUp(int x, int y, int button, int mods) {
    (void)button; (void)mods;
    switch (drag_mode_) {
    case DragMode::NodeDrag: {
        if (std::abs(drag_dx_) > 0.01 || std::abs(drag_dy_) > 0.01) {
            auto& hist = *Current().history;
            hist.BeginMacro("Move nodes");
            int moved = 0;
            for (auto& [id, orig] : drag_orig_) {
                // Same SnapWorld as the live drag — the committed position
                // must equal what was on screen, and a snapped-back node
                // must not create a no-op undo entry.
                const double nx = SnapWorld(orig.first + drag_dx_);
                const double ny = SnapWorld(orig.second + drag_dy_);
                if (nx != orig.first || ny != orig.second) {
                    hist.MoveNode(id, nx, ny);
                    ++moved;
                }
            }
            hist.EndMacro();
            if (moved > 0) ScheduleAutosave();
        }
        drag_offset_.clear();
        drag_orig_.clear();
        break;
    }
    case DragMode::LinkDrag: {
        const auto drop = ResolveDropPort(x, y, !link_from_is_output_);
        if (drop.node != NG::kInvalidNode) {
            const NG::PortRef from = link_from_is_output_ ? link_from_ : drop;
            const NG::PortRef to = link_from_is_output_ ? drop : link_from_;
            std::string err;
            const NG::LinkId new_id =
                Current().history->Connect(from, to, &err);
            if (new_id != NG::kInvalidLink) {
                // Re-route: the dragged end left its old link — remove it
                // once the new connection is in place.
                if (reroute_link_ != NG::kInvalidLink &&
                    reroute_link_ != new_id) {
                    Current().history->Disconnect(reroute_link_);
                }
                Toast("Connected " + from.port + " -> " + to.port);
                ScheduleAutosave();
            } else {
                // Failed connects are routine while dragging — report in the
                // status bar only, no intrusive error toast.
                SetStatus(err.empty() ? "Cannot connect ports" : err);
            }
        }
        link_dragging_ = false;
        drop_valid_ = false;
        reroute_link_ = NG::kInvalidLink;
        break;
    }
    case DragMode::ParamDrag:
        param_node_ = NG::kInvalidNode;
        param_name_.clear();
        param_kind_ = -1;
        break;
    case DragMode::BoxSelect: {
        const double wx0 = std::min(ToWorldX(box_rect_.x), ToWorldX(x));
        const double wx1 = std::max(ToWorldX(box_rect_.x), ToWorldX(x));
        const double wy0 = std::min(ToWorldY(box_rect_.y), ToWorldY(y));
        const double wy1 = std::max(ToWorldY(box_rect_.y), ToWorldY(y));
        auto& sel = Current().selected_nodes;
        if ((mods & 1) == 0) sel.clear();
        for (const auto& [id, g] : geoms_) {
            if (g.x < wx1 && g.x + g.w > wx0 && g.y < wy1 && g.y + g.h > wy0) {
                sel.insert(id);
            }
        }
        box_active_ = false;
        break;
    }
    case DragMode::Pan:
    case DragMode::None:
        break;
    }
    drag_mode_ = DragMode::None;
    canvas_.Invalidate();
}

void NodeGraphEditorWidget::HandleKey(unsigned msg, int key, int mods) {
    (void)msg;
    const bool ctrl = (mods & 2) != 0;
    const bool shift = (mods & 1) != 0;

    if (palette_open_) {
        switch (key) {
        case VK_UP:
            if (palette_hover_ > 0) --palette_hover_;
            EnsurePaletteItemVisible(palette_hover_);
            canvas_.Invalidate();
            return;
        case VK_DOWN:
            if (palette_hover_ < static_cast<int>(palette_items_.size()) - 1) ++palette_hover_;
            EnsurePaletteItemVisible(palette_hover_);
            canvas_.Invalidate();
            return;
        case VK_RETURN:
            if (palette_hover_ >= 0) AddNodeAtPalette(palette_hover_);
            return;
        case VK_BACK:
            if (!palette_filter_.empty()) {
                palette_filter_.pop_back();
                RebuildPaletteItems();
            }
            canvas_.Invalidate();
            return;
        case VK_ESCAPE:
            ClosePalette();
            return;
        default:
            return;
        }
    }
    if (menu_open_) {
        switch (key) {
        case VK_ESCAPE: CloseMenu(); return;
        case VK_UP: {
            const int n = NextMenuSelectable(menu_hover_, -1);
            if (n >= 0 && n != menu_hover_) {
                menu_hover_ = n;
                EnsureMenuItemVisible(n);
                canvas_.Invalidate();
            }
            return;
        }
        case VK_DOWN: {
            const int n = NextMenuSelectable(menu_hover_, +1);
            if (n >= 0 && n != menu_hover_) {
                menu_hover_ = n;
                EnsureMenuItemVisible(n);
                canvas_.Invalidate();
            }
            return;
        }
        case VK_RETURN:
            if (menu_hover_ >= 0) ActivateMenuEntry(menu_hover_);
            return;
        default: return;
        }
    }
    if (renaming_) {
        switch (key) {
        case VK_RETURN: CommitRename(); return;
        case VK_ESCAPE: renaming_ = false; rename_node_ = NG::kInvalidNode; canvas_.Invalidate(); return;
        case VK_BACK:
            if (!rename_buffer_.empty()) rename_buffer_.pop_back();
            canvas_.Invalidate();
            return;
        default: return;
        }
    }

    if (!ctrl) {
        switch (key) {
        case VK_DELETE: DeleteSelection(); return;
        case VK_ESCAPE:
            Current().selected_nodes.clear();
            Current().selected_link.reset();
            canvas_.Invalidate();
            return;
        case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN: {
            const double step = shift ? 16.0 : 4.0;
            double dx = 0, dy = 0;
            if (key == VK_LEFT) dx = -step; else if (key == VK_RIGHT) dx = step;
            else if (key == VK_UP) dy = -step; else dy = step;
            auto& hist = *Current().history;
            hist.BeginMacro("Nudge");
            for (auto id : Current().selected_nodes) {
                if (const NG::Node* n = Current().graph->FindNode(id)) {
                    hist.MoveNode(id, n->x + dx, n->y + dy);
                }
            }
            hist.EndMacro();
            if (!Current().selected_nodes.empty()) ScheduleAutosave();
            return;
        }
        default: return;
        }
    }
    switch (key) {
    case 'Z': case 'z':
        if (shift) Redo(); else Undo();
        return;
    case 'Y': case 'y': Redo(); return;
    case 'A': case 'a': SelectAll(); return;
    case 'S': case 's': SaveGraph(); return;
    case 'O': case 'o': LoadGraph(); return;
    case 'R': case 'r': RunGraph(); return;
    case 'C': case 'c': CopySelection(false); return;
    case 'X': case 'x': CopySelection(true); return;
    case 'V': case 'v': PasteClipboard(); return;
    case 'D': case 'd': DuplicateSelection(); return;
    default: return;
    }
}

void NodeGraphEditorWidget::HandleChar(int ch) {
    // WM_CHAR delivers UTF-16 code units — pair surrogates, then append the
    // UTF-8 encoding so non-ASCII input (e.g. Russian) works in the palette
    // filter and the rename field.
    std::string* target = palette_open_ ? &palette_filter_
                                        : (renaming_ ? &rename_buffer_ : nullptr);
    if (target == nullptr) {
        return;
    }
    const std::uint32_t unit = static_cast<std::uint32_t>(ch) & 0xFFFFu;
    if (unit >= 0xD800 && unit <= 0xDBFF) {
        pending_high_surrogate_ = unit;
        return;
    }
    std::uint32_t cp = unit;
    if (unit >= 0xDC00 && unit <= 0xDFFF) {
        if (pending_high_surrogate_ != 0) {
            cp = 0x10000u + ((pending_high_surrogate_ - 0xD800u) << 10) +
                 (unit - 0xDC00u);
        } else {
            cp = 0xFFFD;  // lone low surrogate
        }
    } else if (pending_high_surrogate_ != 0) {
        AppendUtf8(*target, 0xFFFD);  // dangling high surrogate
    }
    pending_high_surrogate_ = 0;
    if (cp < 0x20 || cp == 0x7F) {
        return;
    }
    AppendUtf8(*target, cp);
    if (palette_open_) {
        RebuildPaletteItems();
    }
    canvas_.Invalidate();
}

// ── Editing ops ────────────────────────────────────────────────────────────

void NodeGraphEditorWidget::DeleteSelection() {
    auto& sel = Current().selected_nodes;
    if (!sel.empty()) {
        auto& hist = *Current().history;
        hist.BeginMacro("Delete nodes");
        for (auto id : sel) hist.RemoveNode(id);
        hist.EndMacro();
        sel.clear();
        ScheduleAutosave();
    }
    if (Current().selected_link) {
        Current().history->Disconnect(*Current().selected_link);
        Current().selected_link.reset();
        ScheduleAutosave();
    }
    canvas_.Invalidate();
}

void NodeGraphEditorWidget::SelectAll() {
    Current().selected_nodes.clear();
    for (const auto& [id, n] : Current().graph->nodes()) {
        Current().selected_nodes.insert(id);
    }
    canvas_.Invalidate();
}

// ── Clipboard (system clipboard, JSON graph payload) ───────────────────────

std::string NodeGraphEditorWidget::SerializeSelection() {
    const auto& sel = Current().selected_nodes;
    if (sel.empty()) return {};

    NG::Graph clip;
    for (const auto id : sel) {
        const NG::Node* n = Current().graph->FindNode(id);
        if (n == nullptr) continue;
        NG::Node copy = *n;
        if (n->subgraph) {
            copy.subgraph = std::make_shared<NG::Graph>(*n->subgraph);
        }
        std::string err;
        clip.InsertNode(std::move(copy), &err);
    }
    for (const auto& [lid, link] : Current().graph->links()) {
        if (!sel.count(link.from.node) || !sel.count(link.to.node)) continue;
        NG::Link cl = link;
        cl.id = clip.next_link_id();
        std::string err;
        clip.InsertLink(cl, &err);
    }
    if (clip.nodes().empty()) return {};
    return NG::SerializeGraph(clip);
}

void NodeGraphEditorWidget::CopySelection(bool cut) {
    const std::string payload = SerializeSelection();
    if (payload.empty()) {
        Toast("Nothing selected to copy", true);
        return;
    }
    if (!XER::Content::Clipboard::SetText(payload)) {
        Toast("Clipboard unavailable", true);
        return;
    }
    paste_offset_count_ = 0;
    Toast(cut ? "Cut to clipboard" : "Copied to clipboard");
    if (cut) DeleteSelection();
}

void NodeGraphEditorWidget::PasteClipboardText(const std::string& text) {
    NG::Graph clip;
    std::string err;
    NG::Json json;
    if (!NG::Json::Parse(text, &json, &err) ||
        !NG::GraphFromJson(json, &clip, &err)) {
        // Not a node-graph payload — leave the clipboard alone.
        Toast("Clipboard does not contain nodes", true);
        return;
    }
    if (clip.nodes().empty()) {
        Toast("Clipboard graph is empty", true);
        return;
    }

    std::vector<NG::Node> nodes;
    nodes.reserve(clip.nodes().size());
    for (const auto& [id, n] : clip.nodes()) {
        NG::Node c = n;
        if (n.subgraph) {
            c.subgraph = std::make_shared<NG::Graph>(*n.subgraph);
        }
        nodes.push_back(std::move(c));
    }
    std::vector<NG::Link> links;
    for (const auto& [id, l] : clip.links()) links.push_back(l);

    const double step = kPasteStep * static_cast<double>(paste_offset_count_ + 1);
    ++paste_offset_count_;

    auto& lvl = Current();
    const NG::NodeId first_new = lvl.graph->next_node_id();
    if (!lvl.history->Execute(std::make_unique<PasteGraphCommand>(
            std::move(nodes), std::move(links), step, step))) {
        Toast("Paste failed", true);
        return;
    }
    // Select the freshly pasted nodes (ids are consecutive from next_node_id).
    lvl.selected_nodes.clear();
    lvl.selected_link.reset();
    for (const auto& [id, n] : lvl.graph->nodes()) {
        if (id >= first_new) lvl.selected_nodes.insert(id);
    }
    ScheduleAutosave();
    canvas_.Invalidate();
}

void NodeGraphEditorWidget::PasteClipboard() {
    std::string text;
    if (!XER::Content::Clipboard::GetText(text) || text.empty()) {
        Toast("Clipboard is empty", true);
        return;
    }
    PasteClipboardText(text);
}

void NodeGraphEditorWidget::DuplicateSelection() {
    const std::string payload = SerializeSelection();
    if (payload.empty()) {
        Toast("Nothing selected to duplicate", true);
        return;
    }
    PasteClipboardText(payload);
}

// ── Align / distribute ─────────────────────────────────────────────────────

void NodeGraphEditorWidget::AlignSelection(int mode) {
    auto& sel = Current().selected_nodes;
    if (sel.size() < 2) {
        Toast("Select 2+ nodes to align", true);
        return;
    }
    RebuildGeometries();

    double lo_x = 1e30, hi_x = -1e30, lo_y = 1e30, hi_y = -1e30;
    for (const auto id : sel) {
        const auto it = geoms_.find(id);
        if (it == geoms_.end()) continue;
        const auto& g = it->second;
        lo_x = std::min(lo_x, g.x);
        hi_x = std::max(hi_x, g.x + g.w);
        lo_y = std::min(lo_y, g.y);
        hi_y = std::max(hi_y, g.y + g.h);
    }
    const double cx = (lo_x + hi_x) * 0.5;
    const double cy = (lo_y + hi_y) * 0.5;

    auto& hist = *Current().history;
    hist.BeginMacro("Align nodes");
    for (const auto id : sel) {
        const NG::Node* n = Current().graph->FindNode(id);
        const auto it = geoms_.find(id);
        if (n == nullptr || it == geoms_.end()) continue;
        const auto& g = it->second;
        double nx = n->x, ny = n->y;
        switch (mode) {
        case 0: nx = lo_x; break;
        case 1: nx = cx - g.w * 0.5; break;
        case 2: nx = hi_x - g.w; break;
        case 3: ny = lo_y; break;
        case 4: ny = cy - g.h * 0.5; break;
        case 5: ny = hi_y - g.h; break;
        default: break;
        }
        if (nx != n->x || ny != n->y) hist.MoveNode(id, nx, ny);
    }
    hist.EndMacro();
    ScheduleAutosave();
    canvas_.Invalidate();
}

void NodeGraphEditorWidget::DistributeSelection(bool horizontal) {
    auto& sel = Current().selected_nodes;
    if (sel.size() < 3) {
        Toast("Select 3+ nodes to distribute", true);
        return;
    }
    RebuildGeometries();

    struct Item { NG::NodeId id; double pos, size; };
    std::vector<Item> items;
    for (const auto id : sel) {
        const NG::Node* n = Current().graph->FindNode(id);
        const auto it = geoms_.find(id);
        if (n == nullptr || it == geoms_.end()) continue;
        items.push_back({id, horizontal ? n->x : n->y,
                         horizontal ? it->second.w : it->second.h});
    }
    if (items.size() < 3) {
        Toast("Select 3+ nodes to distribute", true);
        return;
    }
    std::sort(items.begin(), items.end(),
              [](const Item& a, const Item& b) { return a.pos < b.pos; });

    const double first = items.front().pos;
    const double last = items.back().pos + items.back().size;
    const double span = last - first;
    const double total_size =
        std::accumulate(items.begin(), items.end(), 0.0,
                        [](double s, const Item& i) { return s + i.size; });
    const double gap = (span - total_size) / static_cast<double>(items.size() - 1);

    auto& hist = *Current().history;
    hist.BeginMacro(horizontal ? "Distribute horizontally"
                               : "Distribute vertically");
    double cursor = first;
    for (const auto& it : items) {
        const double target = cursor;
        cursor += it.size + gap;
        const NG::Node* n = Current().graph->FindNode(it.id);
        if (n == nullptr) continue;
        const double nx = horizontal ? target : n->x;
        const double ny = horizontal ? n->y : target;
        if (nx != n->x || ny != n->y) hist.MoveNode(it.id, nx, ny);
    }
    hist.EndMacro();
    ScheduleAutosave();
    canvas_.Invalidate();
}

void NodeGraphEditorWidget::Undo() {
    if (Current().history->Undo()) {
        Current().selected_nodes.clear();
        Current().selected_link.reset();
        ScheduleAutosave();
        canvas_.Invalidate();
    }
}

void NodeGraphEditorWidget::Redo() {
    if (Current().history->Redo()) {
        Current().selected_nodes.clear();
        Current().selected_link.reset();
        ScheduleAutosave();
        canvas_.Invalidate();
    }
}

void NodeGraphEditorWidget::RunGraph() {
    debug_nodes_.clear();
    last_result_ = Current().executor->Run();
    has_run_ = true;
    last_run_revision_ = Current().graph->revision();
    error_nodes_.clear();
    node_error_text_.clear();
    for (const auto& [id, msg] : last_result_.node_errors) {
        error_nodes_.insert(id);
        node_error_text_[id] = msg;
    }
    if (last_result_.ok) {
        SetStatus("Run ok — " + std::to_string(last_result_.evaluated) + " eval, " +
                  std::to_string(last_result_.cached) + " cached, " +
                  std::to_string(static_cast<int>(last_result_.elapsed_ms)) + " ms");
        Toast("Graph ran successfully");
    } else {
        SetStatus("Run failed — " + last_result_.error, true);
        Toast(last_result_.error, true);
    }
    // plugin.vst3_view nodes open their editor automatically after a run.
    for (const auto& [id, node] : Current().graph->nodes()) {
        if (node.type == "plugin.vst3_view") {
            OpenPluginEditorFor(id);
            break; // one embedded editor at a time
        }
    }
    canvas_.Invalidate();
}

void NodeGraphEditorWidget::StepGraph() {
    NG::Executor* ex = Current().executor.get();
    NG::Executor::StepInfo info;
    const bool advanced = ex->StepOnce(&info);
    debug_nodes_.clear();
    for (NG::NodeId id : info.nodes) debug_nodes_.insert(id);

    // Mirror run-error state so badges/inspector behave like after Run().
    has_run_ = true;
    last_run_revision_ = Current().graph->revision();
    error_nodes_.clear();
    node_error_text_.clear();
    for (const auto& [id, msg] : ex->step_result().node_errors) {
        error_nodes_.insert(id);
        node_error_text_[id] = msg;
    }

    if (!advanced) {
        // Session finished (or graph has no executable levels).
        const auto& r = ex->step_result();
        SetStatus("Debug complete — " + std::to_string(r.evaluated) +
                  " eval, " + std::to_string(r.cached) + " cached");
        Toast("Step debugging finished");
    } else {
        SetStatus("Step " + std::to_string(info.level + 1) + "/" +
                  std::to_string(info.total_levels) + " — " +
                  std::to_string(info.nodes.size()) + " node(s), level " +
                  std::to_string(info.level + 1));
    }
    if (!ex->step_result().error.empty() && !advanced) {
        SetStatus("Debug failed — " + ex->step_result().error, true);
    }
    canvas_.Invalidate();
}

void NodeGraphEditorWidget::ResetDebug() {
    NG::Executor* ex = Current().executor.get();
    ex->ResetStep();
    ex->InvalidateCache();
    debug_nodes_.clear();
    error_nodes_.clear();
    node_error_text_.clear();
    has_run_ = false;
    SetStatus("Debug session reset — cache cleared");
    canvas_.Invalidate();
}

const std::vector<NG::ValidationIssue>&
NodeGraphEditorWidget::Issues() const {
    const NG::Graph* g = Current().graph;
    if (g != issues_graph_ || g->revision() != issues_rev_) {
        issues_graph_ = g;
        issues_rev_ = g->revision();
        issues_ = g->Validate();
        node_issue_sev_.clear();
        for (const auto& is : issues_) {
            if (is.node == NG::kInvalidNode) continue;
            auto [it, inserted] =
                node_issue_sev_.emplace(is.node, is.severity);
            if (!inserted &&
                is.severity == NG::ValidationIssue::Severity::kError) {
                it->second = is.severity;  // error outranks warning
            }
        }
    }
    return issues_;
}

void NodeGraphEditorWidget::FocusNode(NodeGraph::NodeId id) {
    auto& lvl = Current();
    lvl.selected_nodes.clear();
    lvl.selected_nodes.insert(id);
    lvl.selected_link.reset();
    const auto git = geoms_.find(id);
    if (git != geoms_.end()) {
        const NodeGeom& g = git->second;
        const double cx = g.x + g.w * 0.5, cy = g.y + g.h * 0.5;
        lvl.pan_x = canvas_w_ * 0.5 - cx * lvl.zoom;
        lvl.pan_y = canvas_h_ * 0.5 - cy * lvl.zoom;
    }
    canvas_.Invalidate();
}

void NodeGraphEditorWidget::OpenProblemsMenu(double x, double y) {
    const auto& issues = Issues();
    menu_open_ = true;
    menu_anim_.Set(1.0f, 0.14f);
    menu_is_link_ = false;
    menu_node_ = NG::kInvalidNode;
    menu_link_ = NG::kInvalidLink;
    menu_x_ = x;
    menu_y_ = y;
    menu_hover_ = -1;
    menu_scroll_ = 0.0f;
    if (issues.empty()) {
        menu_entries_ = {{"No problems", "", -1, true}};
        canvas_.Invalidate();
        return;
    }
    menu_entries_.push_back({"Graph problems", "", -1, true});
    menu_entries_.push_back({"", "", -1, false, false, true});
    constexpr size_t kMaxShown = 40;
    for (size_t i = 0; i < issues.size() && i < kMaxShown; ++i) {
        const auto& is = issues[i];
        const bool is_err =
            is.severity == NG::ValidationIssue::Severity::kError;
        std::string label = is_err ? "! " : "~ ";
        if (is.node != NG::kInvalidNode) {
            const NG::Node* n = Current().graph->FindNode(is.node);
            label += (n != nullptr && !n->title.empty()) ? n->title
                                                        : "node";
            label += ": ";
        }
        label += is.message;
        if (label.size() > 64) label = label.substr(0, 63) + "…";
        menu_entries_.push_back(
            {label, "", 40 + static_cast<int>(i), false, is_err});
    }
    if (issues.size() > kMaxShown) {
        menu_entries_.push_back(
            {std::string("…and ") +
                 std::to_string(issues.size() - kMaxShown) + " more",
             "", -1, true});
    }
    canvas_.Invalidate();
}

void NodeGraphEditorWidget::ActivateProblem(int index) {
    Issues();  // refresh — the menu may be older than the last edit
    if (index < 0 || index >= static_cast<int>(issues_.size())) return;
    const auto& is = issues_[index];
    if (is.node != NG::kInvalidNode) {
        FocusNode(is.node);
        SetStatus(is.message);
    } else {
        SetStatus(is.message,
                  is.severity == NG::ValidationIssue::Severity::kError);
    }
    canvas_.Invalidate();
}

void NodeGraphEditorWidget::GroupSelection() {
    NG::Graph& g = *Current().graph;
    // World-space bbox of the selected nodes (geoms carry real sizes).
    double x0 = 1e30, y0 = 1e30, x1 = -1e30, y1 = -1e30;
    int n = 0;
    for (auto id : Current().selected_nodes) {
        const auto git = geoms_.find(id);
        if (git == geoms_.end()) continue;
        const NodeGeom& gg = git->second;
        x0 = std::min(x0, gg.x);
        y0 = std::min(y0, gg.y);
        x1 = std::max(x1, gg.x + gg.w);
        y1 = std::max(y1, gg.y + gg.h);
        ++n;
    }
    if (n == 0) {
        SetStatus("Select nodes to group");
        return;
    }
    static const char* kPalette[] = {
        "#6366f1", "#0ea5e9", "#10b981", "#f59e0b",
        "#ef4444", "#ec4899", "#8b5cf6", "#14b8a6"};
    const size_t slot = g.groups().size() % 8;
    NG::Group grp;
    grp.title = "Group " + std::to_string(g.groups().size() + 1);
    grp.x = SnapWorld(x0 - 26.0);
    grp.y = SnapWorld(y0 - 30.0);
    grp.w = SnapWorld(x1 - x0 + 52.0);
    grp.h = SnapWorld(y1 - y0 + 56.0);
    grp.color = kPalette[slot];
    if (Current().history->AddGroup(std::move(grp))) {
        ScheduleAutosave();
        Toast("Grouped " + std::to_string(n) + " node(s)");
    }
    canvas_.Invalidate();
}

void NodeGraphEditorWidget::UngroupSelection() {
    NG::Graph& g = *Current().graph;
    // Selection bbox in world space; groups intersecting it are removed.
    double x0 = 1e30, y0 = 1e30, x1 = -1e30, y1 = -1e30;
    int n = 0;
    for (auto id : Current().selected_nodes) {
        const auto git = geoms_.find(id);
        if (git == geoms_.end()) continue;
        const NodeGeom& gg = git->second;
        x0 = std::min(x0, gg.x);
        y0 = std::min(y0, gg.y);
        x1 = std::max(x1, gg.x + gg.w);
        y1 = std::max(y1, gg.y + gg.h);
        ++n;
    }
    if (n == 0) {
        SetStatus("Select nodes inside a group to ungroup");
        return;
    }
    std::vector<uint64_t> doomed;
    for (const auto& [gid, grp] : g.groups()) {
        if (grp.x < x1 && grp.x + grp.w > x0 &&
            grp.y < y1 && grp.y + grp.h > y0) {
            doomed.push_back(gid);
        }
    }
    if (doomed.empty()) {
        SetStatus("No group intersects the selection");
        return;
    }
    Current().history->BeginMacro("Ungroup");
    for (uint64_t gid : doomed) Current().history->RemoveGroup(gid);
    Current().history->EndMacro();
    ScheduleAutosave();
    Toast("Removed " + std::to_string(doomed.size()) + " group(s)");
    canvas_.Invalidate();
}

void NodeGraphEditorWidget::ToggleNodeCollapse(NodeGraph::NodeId id) {
    auto& set = Current().collapsed_nodes;
    if (set.erase(id) == 0) {
        set.insert(id);
    }
    canvas_.Invalidate();
}

void NodeGraphEditorWidget::NewGraph() {
    while (levels_.size() > 1) levels_.pop_back();
    auto& lvl = Current();
    lvl.graph->Clear();
    lvl.history->Clear();
    lvl.executor->InvalidateCache();
    lvl.selected_nodes.clear();
    lvl.selected_link.reset();
    lvl.seen_revision = lvl.graph->revision();
    has_run_ = false;
    error_nodes_.clear();
    node_error_text_.clear();
    project_id_ = -1;  // detached from any project
    autosave_graph_name_ = "main";
    pending_file_path_.clear();
    ScheduleAutosave();
    canvas_.Invalidate();
}

void NodeGraphEditorWidget::FitView() {
    if (geoms_.empty()) {
        Current().pan_x = Current().pan_y = 0.0;
        Current().zoom = 1.0;
        canvas_.Invalidate();
        return;
    }
    RebuildGeometries();
    const double bw = world_max_x_ - world_min_x_ + 60.0;
    const double bh = world_max_y_ - world_min_y_ + 60.0;
    const double z = std::clamp(std::min(canvas_w_ / bw, canvas_h_ / bh), 0.2, 1.5);
    Current().zoom = z;
    Current().pan_x = canvas_w_ * 0.5 - (world_min_x_ + world_max_x_) * 0.5 * z;
    Current().pan_y = canvas_h_ * 0.5 - (world_min_y_ + world_max_y_) * 0.5 * z;
    canvas_.Invalidate();
}

void NodeGraphEditorWidget::EnterSubgraph(NodeGraph::NodeId id) {
    const NG::Node* n = Current().graph->FindNode(id);
    if (n == nullptr || n->type != "graph.subgraph" || !n->subgraph) return;
    Level lvl;
    lvl.graph = n->subgraph.get();
    lvl.history = std::make_unique<NG::History>(*lvl.graph);
    lvl.executor = std::make_unique<NG::Executor>(*lvl.graph);
    lvl.crumb = n->title.empty() ? n->type : n->title;
    lvl.owner_node = id;
    lvl.zoom = Current().zoom;
    lvl.pan_x = Current().pan_x;
    lvl.pan_y = Current().pan_y;
    levels_.push_back(std::move(lvl));
    ClosePalette();
    CloseMenu();
    canvas_.Invalidate();
}

void NodeGraphEditorWidget::ExitSubgraph() {
    if (levels_.size() <= 1) return;
    const NG::NodeId owner = levels_.back().owner_node;
    levels_.pop_back();
    // The inner graph may have gained/lost graph.input/graph.output nodes —
    // re-derive the parent node's ports so links stay valid.
    if (owner != NG::kInvalidNode) {
        if (Current().graph->RefreshSubgraphPorts(owner)) {
            ScheduleAutosave();
        }
    }
    canvas_.Invalidate();
}

void NodeGraphEditorWidget::CollapseSelection() {
    std::vector<NG::NodeId> ids(Current().selected_nodes.begin(),
                              Current().selected_nodes.end());
    if (ids.size() < 2) {
        Toast("Select 2+ nodes to collapse", true);
        return;
    }
    std::string err;
    const NG::NodeId sub = Current().history->CollapseToSubgraph(ids, &err);
    if (sub == NG::kInvalidNode) {
        Toast(err.empty() ? "Collapse failed" : err, true);
        return;
    }
    Current().selected_nodes = {sub};
    ScheduleAutosave();
    Toast("Collapsed into subgraph");
    canvas_.Invalidate();
}

// ── Palette / menu / rename ────────────────────────────────────────────────

void NodeGraphEditorWidget::OpenPalette(double sx, double sy) {
    palette_open_ = true;
    palette_filter_.clear();
    palette_scroll_ = 0.0f;
    palette_anchor_x_ = sx;
    palette_anchor_y_ = sy;
    palette_anim_.Set(1.0f, 0.16f);
    RebuildPaletteItems();
    canvas_.Invalidate();
}

void NodeGraphEditorWidget::ClosePalette() {
    palette_open_ = false;
    palette_filter_.clear();
    palette_scroll_ = 0.0f;
    canvas_.Invalidate();
}

void NodeGraphEditorWidget::AddNodeAtPalette(int index) {
    if (index < 0 || index >= static_cast<int>(palette_types_.size())) return;
    const std::string type = palette_types_[index];
    const double wx = SnapWorld(ToWorldX(palette_anchor_x_));
    const double wy = SnapWorld(ToWorldY(palette_anchor_y_));
    std::string err;
    const NG::NodeId id = Current().history->AddNode(type, wx, wy, &err);
    if (id == NG::kInvalidNode) {
        Toast(err.empty() ? "Cannot add node" : err, true);
    } else {
        Current().selected_nodes = {id};
        Current().selected_link.reset();
        ScheduleAutosave();
        Toast("Added " + type);
    }
    ClosePalette();
    canvas_.Invalidate();
}

void NodeGraphEditorWidget::OpenNodeMenu(NodeGraph::NodeId id, double sx, double sy) {
    menu_open_ = true;
    menu_anim_.Set(1.0f, 0.14f);
    menu_is_link_ = false;
    menu_node_ = id;
    menu_link_ = NG::kInvalidLink;
    menu_x_ = sx;
    menu_y_ = sy;
    menu_hover_ = -1;
    menu_scroll_ = 0.0f;
    menu_entries_ = {
        {"Delete", "Del", 0, false, true},
        {"Rename…", "", 1},
        {"Auto-connect", "", 32},
        {"Collapse", "", 36},
        {"", "", -1, false, false, true},
        {"Copy", "Ctrl+C", 20},
        {"Cut", "Ctrl+X", 21},
        {"Paste", "Ctrl+V", 22},
        {"Duplicate", "Ctrl+D", 23},
    };
    for (auto& e : menu_entries_) {
        if (e.action == 36) {
            e.label = Current().collapsed_nodes.count(id) ? "Expand" : "Collapse";
        }
    }
    if (Current().selected_nodes.size() >= 2) {
        menu_entries_.push_back({"", "", -1, false, false, true});
        menu_entries_.push_back({"Align Left", "", 24});
        menu_entries_.push_back({"Align Center", "", 25});
        menu_entries_.push_back({"Align Right", "", 26});
        menu_entries_.push_back({"Align Top", "", 27});
        menu_entries_.push_back({"Align Middle", "", 28});
        menu_entries_.push_back({"Align Bottom", "", 29});
        menu_entries_.push_back({"", "", -1, false, false, true});
        menu_entries_.push_back({"Distribute Horizontally", "", 30});
        menu_entries_.push_back({"Distribute Vertically", "", 31});
    }
    if (const NG::Node* n = Current().graph->FindNode(id);
        n != nullptr && n->type == "graph.subgraph" && n->subgraph) {
        menu_entries_.push_back({"", "", -1, false, false, true});
        menu_entries_.push_back({"Enter Subgraph", "", 2});
    }
    if (Current().selected_nodes.size() >= 2) {
        menu_entries_.push_back({"", "", -1, false, false, true});
        menu_entries_.push_back({"Collapse to Subgraph", "", 3});
    }
    if (plugin_editor_handler_) {
        const NG::Node* n = Current().graph->FindNode(id);
        if (n != nullptr && n->type.rfind("plugin.", 0) == 0) {
            menu_entries_.push_back({"", "", -1, false, false, true});
            menu_entries_.push_back({"Open Plugin Editor", "", 5});
        }
    }
    canvas_.Invalidate();
}

void NodeGraphEditorWidget::OpenPluginEditorFor(NodeGraph::NodeId id) {
    if (!plugin_editor_handler_) return;
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
    canvas_.GetBounds(x, y, w, h);
    if (w < 50.0f || h < 50.0f) return;
    plugin_editor_handler_(id, x, y, w, h);
}

void NodeGraphEditorWidget::OpenCanvasMenu(double sx, double sy) {
    menu_open_ = true;
    menu_anim_.Set(1.0f, 0.14f);
    menu_is_link_ = false;
    menu_node_ = NG::kInvalidNode;
    menu_link_ = NG::kInvalidLink;
    menu_x_ = sx;
    menu_y_ = sy;
    menu_hover_ = -1;
    menu_scroll_ = 0.0f;
    const bool can_undo = Current().history && Current().history->CanUndo();
    const bool can_redo = Current().history && Current().history->CanRedo();
    menu_entries_ = {
        {"Add Node…", "", 10},
        {"", "", -1, false, false, true},
        {"Copy", "Ctrl+C", 20, Current().selected_nodes.empty()},
        {"Cut", "Ctrl+X", 21, Current().selected_nodes.empty()},
        {"Paste", "Ctrl+V", 22},
        {"Duplicate", "Ctrl+D", 23, Current().selected_nodes.empty()},
        {"Undo", "Ctrl+Z", 11, !can_undo},
        {"Redo", "Ctrl+Y", 12, !can_redo},
        {"Run Graph", "Ctrl+R", 13},
        {"Step Graph", "", 40},
        {"Reset Debug", "", 41},
        {"Auto-connect", "", 32,
         Current().graph->nodes().empty()},
        {"", "", -1, false, false, true},
        {"Fit View", "", 14},
        {"", "", -1, false, false, true},
        {"Problems", "", 33, Issues().empty()},
        {"Group Selection", "", 34, Current().selected_nodes.empty()},
        {"Ungroup Selection", "", 35, Current().selected_nodes.empty()},
    };
    if (Current().selected_nodes.size() >= 2) {
        menu_entries_.push_back({"", "", -1, false, false, true});
        menu_entries_.push_back({"Align Left", "", 24});
        menu_entries_.push_back({"Align Center", "", 25});
        menu_entries_.push_back({"Align Right", "", 26});
        menu_entries_.push_back({"Align Top", "", 27});
        menu_entries_.push_back({"Align Middle", "", 28});
        menu_entries_.push_back({"Align Bottom", "", 29});
        menu_entries_.push_back({"", "", -1, false, false, true});
        menu_entries_.push_back({"Distribute Horizontally", "", 30});
        menu_entries_.push_back({"Distribute Vertically", "", 31});
    }
    menu_entries_.push_back({"", "", -1, false, false, true});
    menu_entries_.push_back({"New Graph", "", 15, false, true});
    menu_entries_.push_back({"Save Graph…", "Ctrl+S", 16});
    menu_entries_.push_back({"Load Graph…", "Ctrl+O", 17});
    canvas_.Invalidate();
}

void NodeGraphEditorWidget::OpenLinkMenu(NodeGraph::LinkId id, double sx, double sy) {
    menu_open_ = true;
    menu_anim_.Set(1.0f, 0.14f);
    menu_is_link_ = true;
    menu_node_ = NG::kInvalidNode;
    menu_link_ = id;
    menu_x_ = sx;
    menu_y_ = sy;
    menu_hover_ = -1;
    menu_scroll_ = 0.0f;
    menu_entries_ = {{"Remove Link", "Del", 4, false, true}};
    canvas_.Invalidate();
}

void NodeGraphEditorWidget::CloseMenu() {
    menu_open_ = false;
    menu_entries_.clear();
    menu_hover_ = -1;
    menu_scroll_ = 0.0f;
    canvas_.Invalidate();
}

int NodeGraphEditorWidget::NextMenuSelectable(int from, int dir) const {
    const int n = static_cast<int>(menu_entries_.size());
    if (n == 0) return -1;
    auto selectable = [this](int i) {
        const auto& e = menu_entries_[i];
        return !e.separator && !e.disabled && e.action >= 0;
    };
    if (from < 0 || from >= n) {
        // Start from just outside the list in the travel direction.
        int i = dir >= 0 ? 0 : n - 1;
        while (i >= 0 && i < n && !selectable(i)) i += dir;
        return (i >= 0 && i < n) ? i : -1;
    }
    if (dir == 0) return selectable(from) ? from : -1;
    int i = from + dir;
    while (i >= 0 && i < n && !selectable(i)) i += dir;
    return (i >= 0 && i < n) ? i : -1;
}

void NodeGraphEditorWidget::ActivateMenuEntry(int idx) {
    if (idx < 0 || idx >= static_cast<int>(menu_entries_.size())) return;
    const MenuEntry& e = menu_entries_[idx];
    if (e.disabled || e.separator || e.action < 0) return;
    const int action = e.action;
    CloseMenu();
    switch (action) {
    case 0: DeleteSelection(); break;
    case 1: StartRename(menu_node_); break;
    case 2: EnterSubgraph(menu_node_); break;
    case 3: CollapseSelection(); break;
    case 4:
        if (Current().history) Current().history->Disconnect(menu_link_);
        Current().selected_link.reset();
        canvas_.Invalidate();
        break;
    case 5: OpenPluginEditorFor(menu_node_); break;
    case 10: OpenPalette(menu_x_, menu_y_); break;
    case 11: Undo(); break;
    case 12: Redo(); break;
    case 13: RunGraph(); break;
    case 40: StepGraph(); break;
    case 41: ResetDebug(); break;
    case 14: FitView(); break;
    case 15: NewGraph(); break;
    case 16: SaveGraph(); break;
    case 17: LoadGraph(); break;
    case 20: CopySelection(false); break;
    case 21: CopySelection(true); break;
    case 22: PasteClipboard(); break;
    case 23: DuplicateSelection(); break;
    case 24: AlignSelection(0); break;
    case 25: AlignSelection(1); break;
    case 26: AlignSelection(2); break;
    case 27: AlignSelection(3); break;
    case 28: AlignSelection(4); break;
    case 29: AlignSelection(5); break;
    case 30: DistributeSelection(true); break;
    case 31: DistributeSelection(false); break;
    case 32: {
        const int n = Autoconnect();
        if (n > 0) {
            Toast("Auto-connected " + std::to_string(n) + " link(s)");
        } else {
            SetStatus("Nothing to auto-connect");
        }
        break;
    }
    case 33:
        // Reopen the canvas menu as the problems list at the same spot.
        OpenProblemsMenu(menu_x_, menu_y_);
        break;
    case 34: GroupSelection(); break;
    case 35: UngroupSelection(); break;
    case 36: ToggleNodeCollapse(menu_node_); break;
    default:
        if (action >= 40) ActivateProblem(action - 40);
        break;
    }
}

int NodeGraphEditorWidget::Autoconnect() {
    NG::Graph& g = *Current().graph;
    auto& hist = *Current().history;
    const auto& sel = Current().selected_nodes;
    const bool scoped = !sel.empty();

    struct Cand {
        NG::PortRef port;
        NG::NodeId node = NG::kInvalidNode;
        std::string name;
        NG::ValueType type = NG::ValueType::kAny;
        double x = 0.0, y = 0.0;
        std::string cat;
    };

    auto cat_of = [&](NG::NodeId id) -> std::string {
        const NG::Node* n = g.FindNode(id);
        if (n == nullptr) return {};
        const NG::NodeTypeDescriptor* d = g.registry().Find(n->type);
        return d != nullptr ? d->category : std::string();
    };

    std::vector<Cand> all_outputs;
    std::vector<Cand> scope_inputs;   // free inputs of scoped nodes
    std::vector<Cand> scope_outputs;  // outputs of scoped nodes
    std::vector<Cand> all_free_inputs;

    for (const auto& [id, node] : g.nodes()) {
        const bool in_scope = !scoped || sel.count(id) != 0;
        const std::string cat = cat_of(id);
        for (const auto& p : node.outputs) {
            Cand c{{id, p.name}, id, p.name, p.type, node.x, node.y, cat};
            all_outputs.push_back(c);
            if (in_scope) scope_outputs.push_back(c);
        }
        for (const auto& p : node.inputs) {
            if (g.FindLinkTo({id, p.name}) != nullptr) continue;
            Cand c{{id, p.name}, id, p.name, p.type, node.x, node.y, cat};
            all_free_inputs.push_back(c);
            if (in_scope) scope_inputs.push_back(c);
        }
    }

    // Score = type-compatible (hard filter upstream) + name affinity +
    // same-category bonus - distance penalty. Candidates are ranked per
    // target and Connect() itself re-validates cycles/types, so the first
    // successful attempt wins.
    auto score = [](const Cand& s, const Cand& t) {
        double v = 0.0;
        if (s.name == t.name) {
            v += 4.0;
        } else {
            const std::string ls = XER::Helper::String::ToLowerAscii(s.name);
            const std::string lt = XER::Helper::String::ToLowerAscii(t.name);
            if (!ls.empty() && !lt.empty() &&
                (ls.find(lt) != std::string::npos ||
                 lt.find(ls) != std::string::npos)) {
                v += 2.0;
            }
        }
        if (!s.cat.empty() && s.cat == t.cat) v += 1.0;
        v -= std::min(2.0, std::abs(s.x - t.x) / 300.0);
        v -= std::min(1.0, std::abs(s.y - t.y) / 400.0);
        return v;
    };

    int added = 0;
    hist.BeginMacro("Auto-connect");

    auto wire_pass = [&](const std::vector<Cand>& sources,
                         std::vector<Cand>& targets) {
        for (const Cand& t : targets) {
            if (g.FindLinkTo(t.port) != nullptr) continue;  // wired already
            std::vector<std::pair<double, const Cand*>> ranked;
            for (const Cand& s : sources) {
                if (s.node == t.node) continue;
                if (!NG::AreTypesCompatible(s.type, t.type)) continue;
                ranked.emplace_back(score(s, t), &s);
            }
            std::sort(ranked.begin(), ranked.end(),
                      [](const auto& a, const auto& b) { return a.first > b.first; });
            const size_t tries = std::min<size_t>(ranked.size(), 6);
            for (size_t i = 0; i < tries; ++i) {
                if (hist.Connect(ranked[i].second->port, t.port, nullptr) !=
                    NG::kInvalidLink) {
                    ++added;
                    break;
                }
            }
        }
    };

    if (scoped) {
        // Both directions for the selection: its free inputs fed from
        // anywhere, its free outputs feeding any free input.
        wire_pass(all_outputs, scope_inputs);
        wire_pass(scope_outputs, all_free_inputs);
    } else {
        wire_pass(all_outputs, all_free_inputs);
    }

    hist.EndMacro();
    if (added > 0) {
        ScheduleAutosave();
        canvas_.Invalidate();
    }
    return added;
}

void NodeGraphEditorWidget::OpenParamFileBrowse(
    NodeGraph::NodeId node, const std::string& param_name) {
#ifdef _WIN32
    const std::wstring picked = OpenFileDialog(
        static_cast<HWND>(parent_hwnd_),
        L"All files\0*.*\0", nullptr, L"Open file", false);
    if (picked.empty()) return;
    Current().history->SetParam(node, param_name,
                                NG::Value(Utf8FromWide(picked)));
    ScheduleAutosave();
#endif
    (void)node;
    (void)param_name;
}

bool NodeGraphEditorWidget::OnChromeContextMenu(const Event& event) {
    // Right-click on chrome (nav bar, status bar, inspector gaps) that no
    // widget consumed -> app-level menu via the widget-tree overlay.
    if (event.type != Event::Type::MouseDown ||
        event.mouse_button != MouseButton::Right) {
        return false;
    }
    using Item = ContextMenu::Item;
    std::vector<Item> items;
    items.push_back({"New Graph", [this] { NewGraph(); }, true, false, false, "",
                     true});
    items.push_back({"Save Graph…", [this] { SaveGraph(); }, true, false, false, "Ctrl+S"});
    items.push_back({"Load Graph…", [this] { LoadGraph(); }, true, false, false, "Ctrl+O"});
    items.push_back({"Undo", [this] { Undo(); }, Current().history->CanUndo(),
                     true, false, "Ctrl+Z"});
    items.push_back({"Redo", [this] { Redo(); }, Current().history->CanRedo(),
                     false, false, "Ctrl+Shift+Z"});
    items.push_back({"Run Graph", [this] { RunGraph(); }, true, true, false, "Ctrl+R"});
    items.push_back({"Fit View", [this] { FitView(); }, true, false, false, ""});
    items.push_back({"Open Settings", [this] { SetView(2); },
                     true, true, false, ""});

    auto menu = ContextMenu::Create(std::move(items));
    menu->SetAnchor(event.position);
    ShowOverlay(menu);
    return true;
}

void NodeGraphEditorWidget::StartRename(NodeGraph::NodeId id) {
    const NG::Node* n = Current().graph->FindNode(id);
    if (n == nullptr) return;
    renaming_ = true;
    rename_node_ = id;
    rename_buffer_ = !n->title.empty() ? n->title : n->type;
    canvas_.Invalidate();
}

void NodeGraphEditorWidget::CommitRename() {
    CommitRenameCommand();
    renaming_ = false;
    rename_node_ = NG::kInvalidNode;
    rename_buffer_.clear();
    canvas_.Invalidate();
}

void NodeGraphEditorWidget::CommitRenameCommand() {
    if (rename_node_ == NG::kInvalidNode) return;
    const NG::Node* n = Current().graph->FindNode(rename_node_);
    if (n == nullptr) return;
    const std::string trimmed = rename_buffer_;
    const std::string current = !n->title.empty() ? n->title : n->type;
    // Empty input resets the title to the node-type default.
    if (trimmed != current) {
        Current().history->Execute(
            std::make_unique<RenameNodeCommand>(rename_node_, trimmed));
        ScheduleAutosave();
    }
}

// ── Inspector ──────────────────────────────────────────────────────────────

void NodeGraphEditorWidget::ClearInspector() {
    for (auto& w : inspector_widgets_) {
        RemoveChild(w);
    }
    inspector_widgets_.clear();
}

void NodeGraphEditorWidget::AddParamControl(NodeGraph::Node& node,
                                            const NG::ParamSpec& spec,
                                            float x, float y, float w) {
    const NG::Value cur = node.params.count(spec.name)
        ? node.params.at(spec.name) : spec.default_value;
    auto& hist = *Current().history;
    const NG::NodeId id = node.id;

    if (!spec.options.empty()) {
        auto sel = std::make_shared<Select>();
        sel->SetOptions(spec.options);
        int idx = 0;
        const std::string cur_s = cur.AsString();
        for (size_t i = 0; i < spec.options.size(); ++i) {
            if (spec.options[i] == cur_s) { idx = static_cast<int>(i); break; }
        }
        sel->SetSelectedIndex(idx);
        sel->SetOnChanged([this, id, name = spec.name, opts = spec.options](int i) {
            if (i >= 0 && i < static_cast<int>(opts.size())) {
                Current().history->SetParam(id, name, NG::Value(opts[i]));
                ScheduleAutosave();
            }
        });
        inspector_widgets_.push_back(sel);
        return;
    }
    if (cur.IsNone() ? spec.default_value.Type() == NG::ValueType::kBool
                     : cur.Type() == NG::ValueType::kBool) {
        auto sw = std::make_shared<Switch>(cur.AsBool());
        sw->SetOnChanged([this, id, name = spec.name](bool v) {
            Current().history->SetParam(id, name, NG::Value(v));
            ScheduleAutosave();
        });
        inspector_widgets_.push_back(sw);
        return;
    }
    auto ti = std::make_shared<TextInput>(FormatValue(cur));
    ti->SetOnValueChanged([this, id, name = spec.name, def = spec.default_value](
                              const std::string& text) {
        NG::Value v;
        const NG::ValueType t = def.Type();
        if (t == NG::ValueType::kNumber) {
            try {
                v = NG::Value(std::stod(text));
            } catch (...) { return; }
        } else if (t == NG::ValueType::kArray) {
            NG::Value::Array arr;
            std::stringstream ss(text);
            std::string item;
            while (std::getline(ss, item, ',')) {
                try { arr.push_back(std::stod(item)); } catch (...) {}
            }
            v = NG::Value(std::move(arr));
        } else if (t == NG::ValueType::kBool) {
            v = NG::Value(text == "true" || text == "1");
        } else {
            v = NG::Value(text);
        }
        Current().history->SetParam(id, name, v);
        ScheduleAutosave();
    });
    inspector_widgets_.push_back(ti);
    (void)x; (void)y; (void)w; (void)hist;
}

void NodeGraphEditorWidget::RebuildInspector() {
    // Key: selected node + graph revision + run revision.
    std::string key;
    const auto& sel = Current().selected_nodes;
    if (sel.size() == 1) {
        const NG::NodeId id = *sel.begin();
        key = std::to_string(id) + "|" +
              std::to_string(Current().graph->revision()) + "|" +
              std::to_string(last_run_revision_);
        NG::Node* node = Current().graph->FindNode(id);
        if (node == nullptr) {
            ClearInspector();
            inspector_key_.clear();
            return;
        }
        if (key == inspector_key_) return;
        ClearInspector();
        inspector_key_ = key;

        const NG::NodeTypeDescriptor* desc = Current().graph->registry().Find(node->type);
        auto header = std::make_shared<Label>(
            (!node->title.empty() ? node->title : node->type) +
            "  ·  " + node->type, false);
        inspector_widgets_.push_back(header);

        auto params_label = std::make_shared<Label>("Parameters", true);
        inspector_widgets_.push_back(params_label);

        if (desc != nullptr) {
            for (const auto& spec : desc->params) {
                auto name_label = std::make_shared<Label>(spec.name, true);
                inspector_widgets_.push_back(name_label);
                AddParamControl(*node, spec, 0, 0, 0);
            }
        } else {
            auto none = std::make_shared<Label>("no parameter spec", true);
            inspector_widgets_.push_back(none);
        }

        // Parameter presets (ProjectStore) keyed by node type.
        if (NodeGraph::ProjectStore* store = EnsureProjectStore()) {
            const auto presets = store->ListPresets(node->type);
            auto presets_label = std::make_shared<Label>("Presets", true);
            inspector_widgets_.push_back(presets_label);

            std::vector<std::string> names;
            names.reserve(presets.size());
            for (const auto& p : presets) names.push_back(p.first);
            auto preset_sel = std::make_shared<Select>(names, -1);
            preset_sel->SetPlaceholder("Select preset…");
            inspector_widgets_.push_back(preset_sel);

            auto preset_name = std::make_shared<TextInput>("");
            preset_name->SetPlaceholder("Preset name");
            preset_name->SetTooltip("Preset name");
            inspector_widgets_.push_back(preset_name);

            const NG::NodeId preset_node = id;
            const std::string preset_type = node->type;
            auto refresh_inspector = [this] {
                inspector_key_.clear();
                RebuildInspector();
            };

            auto save_btn = std::make_shared<Button>("Save preset",
                                                     ButtonStyle::Secondary);
            save_btn->SetOnClick([this, store, preset_node, preset_type,
                                  preset_name, refresh_inspector] {
                const std::string pname = preset_name->GetValue();
                if (pname.empty()) {
                    Toast("Preset name required", true);
                    return;
                }
                const NG::Node* n = Current().graph->FindNode(preset_node);
                if (n == nullptr) return;
                NG::Json params = NG::Json::MakeObject();
                for (const auto& [k, v] : n->params) params[k] = v.ToJson();
                std::string err;
                if (store->SavePreset(preset_type, pname, params.Dump(), &err)) {
                    Toast("Preset '" + pname + "' saved");
                    refresh_inspector();
                } else {
                    Toast("Save preset failed — " + err, true);
                }
            });
            inspector_widgets_.push_back(save_btn);

            auto load_btn = std::make_shared<Button>("Load preset",
                                                     ButtonStyle::Outline);
            load_btn->SetOnClick([this, store, preset_node, preset_type,
                                  preset_sel, refresh_inspector] {
                const std::string pname = preset_sel->GetSelectedText();
                if (pname.empty()) {
                    Toast("Select a preset first", true);
                    return;
                }
                std::string json, err;
                if (!store->LoadPreset(preset_type, pname, &json, &err)) {
                    Toast("Load preset failed — " + err, true);
                    return;
                }
                NG::Json pj;
                if (!NG::Json::Parse(json, &pj, &err) || !pj.IsObject()) {
                    Toast("Preset data is corrupt", true);
                    return;
                }
                if (Current().graph->FindNode(preset_node) == nullptr) return;
                auto& hist = *Current().history;
                hist.BeginMacro("Load preset");
                for (const auto& [k, v] : pj.AsObject()) {
                    hist.SetParam(preset_node, k, NG::Value::FromJson(v));
                }
                hist.EndMacro();
                ScheduleAutosave();
                Toast("Preset '" + pname + "' applied");
                refresh_inspector();
            });
            inspector_widgets_.push_back(load_btn);

            auto del_btn = std::make_shared<Button>("Delete preset",
                                                    ButtonStyle::Ghost);
            del_btn->SetOnClick([this, store, preset_type, preset_sel,
                                 refresh_inspector] {
                const std::string pname = preset_sel->GetSelectedText();
                if (pname.empty()) {
                    Toast("Select a preset first", true);
                    return;
                }
                std::string err;
                if (store->DeletePreset(preset_type, pname, &err)) {
                    Toast("Preset '" + pname + "' deleted");
                    refresh_inspector();
                } else {
                    Toast("Delete preset failed — " + err, true);
                }
            });
            inspector_widgets_.push_back(del_btn);
        }

        // Last-run outputs for this node.
        if (has_run_ && Current().executor) {
            if (const auto* outs = Current().executor->GetOutputs(id)) {
                auto out_label = std::make_shared<Label>("Outputs", true);
                inspector_widgets_.push_back(out_label);
                for (const auto& [pname, pval] : *outs) {
                    auto l = std::make_shared<Label>(pname + " = " + FormatValue(pval), false);
                    inspector_widgets_.push_back(l);
                }
            }
            auto eit = node_error_text_.find(id);
            if (eit != node_error_text_.end()) {
                auto el = std::make_shared<Label>("ERROR: " + eit->second, false);
                inspector_widgets_.push_back(el);
            }
        }
        for (auto& w : inspector_widgets_) AddChild(w);
        RequestLayout();
        return;
    }

    // No single selection -> collapse inspector.
    if (!inspector_widgets_.empty() || !inspector_key_.empty()) {
        ClearInspector();
        inspector_key_.clear();
        RequestLayout();
    }
}

// ── Persistence ────────────────────────────────────────────────────────────

void NodeGraphEditorWidget::ScheduleAutosave() {
    autosave_dirty_ = true;
}

NodeGraph::ProjectStore* NodeGraphEditorWidget::EnsureProjectStore() {
    if (project_store_) {
        return project_store_->IsOpen() ? project_store_.get() : nullptr;
    }
    namespace fs = std::filesystem;
    auto& cache = XER::Cache::CacheManager::Instance();
    const auto dir = cache.Persistent().root() / "node_graph";
    std::error_code ec;
    fs::create_directories(dir, ec);
    project_store_ = std::make_unique<NodeGraph::ProjectStore>(
        (dir / "projects.db").string());
    std::string err;
    if (!project_store_->Open(&err)) {
        project_store_.reset();
        return nullptr;
    }
    return project_store_.get();
}

std::int64_t NodeGraphEditorWidget::AutosaveProjectId() {
    // Attached project wins: autosave keeps writing into the project the
    // user opened/saved, not the internal "autosave" bucket.
    if (project_id_ >= 0) {
        return project_id_;
    }
    if (autosave_project_id_ >= 0) {
        return autosave_project_id_;
    }
    NodeGraph::ProjectStore* store = EnsureProjectStore();
    if (!store) return -1;
    for (const auto& p : store->ListProjects()) {
        if (p.name == "autosave") {
            autosave_project_id_ = p.id;
            return p.id;
        }
    }
    std::string err;
    autosave_project_id_ = store->CreateProject("autosave", &err);
    return autosave_project_id_;
}

void NodeGraphEditorWidget::FlushAutosave() {
    const std::string json = NG::SerializeGraph(*levels_.front().graph);
    // Primary: SQLite project store (projects.db under the cache root).
    if (NodeGraph::ProjectStore* store = EnsureProjectStore()) {
        const std::int64_t pid = AutosaveProjectId();
        if (pid >= 0) {
            store->SaveGraph(pid, autosave_graph_name_, json, nullptr);
        }
    }
    // Fallback mirror: cache-text blob (also feeds older recovery paths).
    std::string err;
    if (XER::Cache::CacheManager::Instance().WritePersistentText(
            "ui", "node_graph_editor", "autosave", json, &err)) {
        last_autosave_ = std::chrono::steady_clock::now();
        autosave_dirty_ = false;
    }
}

void NodeGraphEditorWidget::OpenProjectById(std::int64_t pid) {
    OpenProjectGraph(pid, "");
}

void NodeGraphEditorWidget::OpenProjectGraph(std::int64_t pid,
                                             const std::string& graph_name) {
    NodeGraph::ProjectStore* store = EnsureProjectStore();
    if (store == nullptr) {
        SetStatus("Project database is not available", true);
        Toast("Project store unavailable", true);
        return;
    }

    // Pick the graph to load: explicit name wins, else "main", else first.
    const auto graphs = store->ListGraphs(pid);
    std::string resolved = graph_name;
    if (resolved.empty()) {
        for (const auto& g : graphs) {
            if (g.name == "main") {
                resolved = g.name;
                break;
            }
        }
        if (resolved.empty() && !graphs.empty()) {
            resolved = graphs.front().name;
        }
    }
    if (resolved.empty()) resolved = "main";

    bool loaded = false;
    if (!graph_name.empty() || !graphs.empty()) {
        std::string json;
        if (store->LoadGraph(pid, resolved, &json, nullptr) && !json.empty()) {
            NG::Graph tmp;
            loaded = NG::DeserializeGraph(json, &tmp, nullptr);
            if (loaded) {
                while (levels_.size() > 1) levels_.pop_back();
                *levels_.front().graph = tmp;
            }
        }
    }

    // Reset editor state (shared by loaded and empty projects).
    while (levels_.size() > 1) levels_.pop_back();
    auto& lvl = levels_.front();
    if (!loaded) {
        // Empty/missing graph: clear the canvas so autosave doesn't
        // silently write stale content into this project.
        lvl.graph->Clear();
    }
    lvl.history->Clear();
    lvl.executor->InvalidateCache();
    lvl.selected_nodes.clear();
    lvl.selected_link.reset();
    lvl.seen_revision = lvl.graph->revision();
    has_run_ = false;
    error_nodes_.clear();
    node_error_text_.clear();
    pending_file_path_.clear();
    project_id_ = pid;
    autosave_graph_name_ = resolved;
    ScheduleAutosave();

    const std::string what = "Opened '" + resolved + "'";
    SetStatus(loaded ? what : what + " (empty)");
    Toast(loaded ? "Project opened" : "Project is empty");
    canvas_.Invalidate();
    SetView(0);
}

void NodeGraphEditorWidget::SaveCurrentGraphAsProject(const std::string& name) {
    if (name.empty()) {
        SetStatus("Project name required", true);
        return;
    }
    NodeGraph::ProjectStore* store = EnsureProjectStore();
    if (store == nullptr) {
        SetStatus("Project database is not available", true);
        Toast("Project store unavailable", true);
        return;
    }

    // Find-or-create the project by name.
    std::int64_t pid = -1;
    for (const auto& p : store->ListProjects()) {
        if (p.name == name) {
            pid = p.id;
            break;
        }
    }
    if (pid < 0) {
        std::string err;
        pid = store->CreateProject(name, &err);
        if (pid < 0) {
            SetStatus("Create project failed — " + err, true);
            Toast(err, true);
            return;
        }
    }

    const std::string json = NG::SerializeGraph(*levels_.front().graph);
    std::string err;
    if (!store->SaveGraph(pid, "main", json, &err)) {
        SetStatus("Save failed — " + err, true);
        Toast(err, true);
        return;
    }
    project_id_ = pid;
    autosave_graph_name_ = "main";
    pending_file_path_.clear();
    FlushAutosave();
    SetStatus("Saved to project '" + name + "'");
    Toast("Project saved");
}

void NodeGraphEditorWidget::SaveGraph() {
#ifdef _WIN32
    const wchar_t* filter =
        L"Node Graph (*.xerng)\0*.xerng\0JSON (*.json)\0*.json\0All Files (*.*)\0*.*\0";
    std::wstring path;
    if (!pending_file_path_.empty()) {
        path = std::wstring(pending_file_path_.begin(), pending_file_path_.end());
    } else {
        path = OpenFileDialog(static_cast<HWND>(parent_hwnd_), filter, L"xerng",
                              L"Save Node Graph", true);
    }
    if (path.empty()) return;
    pending_file_path_ = Utf8FromWide(path);
    std::string err;
    if (NG::SaveGraphToFile(*levels_.front().graph, pending_file_path_, &err)) {
        SetStatus("Saved " + pending_file_path_);
        Toast("Graph saved");
    } else {
        SetStatus("Save failed — " + err, true);
        Toast(err, true);
    }
#else
    (void)0;
#endif
}

void NodeGraphEditorWidget::LoadGraph() {
#ifdef _WIN32
    while (levels_.size() > 1) levels_.pop_back();
    const wchar_t* filter =
        L"Node Graph (*.xerng;*.json)\0*.xerng;*.json\0JSON (*.json)\0*.json\0All Files (*.*)\0*.*\0";
    const std::wstring path = OpenFileDialog(static_cast<HWND>(parent_hwnd_), filter,
                                             L"xerng", L"Load Node Graph", false);
    if (path.empty()) return;
    const std::string utf8 = Utf8FromWide(path);
    NG::Graph tmp;
    std::string err;
    if (!NG::LoadGraphFromFile(utf8, &tmp, &err)) {
        SetStatus("Load failed — " + err, true);
        Toast(err, true);
        return;
    }
    *levels_.front().graph = tmp;
    levels_.front().history->Clear();
    levels_.front().executor->InvalidateCache();
    levels_.front().selected_nodes.clear();
    levels_.front().selected_link.reset();
    levels_.front().seen_revision = levels_.front().graph->revision();
    pending_file_path_ = utf8;
    project_id_ = -1;  // file-backed, detached from projects
    autosave_graph_name_ = "main";
    has_run_ = false;
    error_nodes_.clear();
    node_error_text_.clear();
    ScheduleAutosave();
    SetStatus("Loaded " + utf8);
    Toast("Graph loaded");
    canvas_.Invalidate();
#else
    (void)0;
#endif
}

// ── Status / tick ──────────────────────────────────────────────────────────

void NodeGraphEditorWidget::SetStatus(const std::string& text, bool is_error) {
    status_text_ = text;
    status_is_error_ = is_error;
    if (status_label_) {
        status_label_->SetText((is_error ? "✕ " : "") + text);
    }
}

void NodeGraphEditorWidget::Toast(const std::string& text, bool is_error) {
    toast_text_ = text;
    toast_is_error_ = is_error;
    toast_anim_.Set(1.0f, 0.18f);
    toast_until_ = std::chrono::steady_clock::now() + std::chrono::seconds(4);
    canvas_.Invalidate();
}

void NodeGraphEditorWidget::SetPluginEditorHandler(
    std::function<void(NodeGraph::NodeId, float, float, float, float)> handler) {
    plugin_editor_handler_ = std::move(handler);
}

void NodeGraphEditorWidget::SetView(int index) {
    if (index == view_) {
        return;
    }
    view_ = index;
    nav_tabs_->SetActiveIndex(index);
    if (view_ == 1 && projects_widget_) {
        projects_widget_->Refresh();
    }
    if (view_ == 2 && settings_widget_) {
        settings_widget_->RefreshDynamic();
    }
    if (video_widget_) {
        video_widget_->SetPreviewActive(view_ == 3);
    }
    canvas_.Invalidate();
    RequestLayout();
}

void NodeGraphEditorWidget::Tick() {
    // Frame dt for the widget-tree motion system (springs/tweens) and the
    // canvas-drawn popover entrances.
    const auto now = std::chrono::steady_clock::now();
    double dt = 0.033;
    if (last_tick_.time_since_epoch().count() != 0) {
        dt = std::chrono::duration<double>(now - last_tick_).count();
    }
    last_tick_ = now;
    dt = std::clamp(dt, 0.0, 0.1);
    TickTree(dt);
    // Overlay widgets (Select dropdowns, ContextMenus, Dialogs) live in the
    // WidgetTree overlay layer, not under this widget's children.
    if (WidgetTree* tree = GetTree()) {
        tree->Tick(dt); // hover-tooltip timing (passive overlay)
        if (auto overlay = tree->GetOverlay()) {
            overlay->TickTree(dt);
        }
    }
    const float fdt = static_cast<float>(dt);
    bool canvas_anim = false;
    if (menu_open_) {
        menu_anim_.Update(fdt);
        canvas_anim |= menu_anim_.Active();
    }
    if (palette_open_) {
        palette_anim_.Update(fdt);
        canvas_anim |= palette_anim_.Active();
    }
    if (!toast_text_.empty()) {
        toast_anim_.Update(fdt);
        canvas_anim |= toast_anim_.Active();
    }
    if (canvas_anim) canvas_.Invalidate();

    auto& lvl = Current();
    const uint64_t rev = lvl.graph->revision();
    if (rev != lvl.seen_revision) {
        lvl.seen_revision = rev;
        autosave_dirty_ = true;
        canvas_.Invalidate();
    }
    // Animated inspector width.
    const float target = lvl.selected_nodes.size() == 1 ? 252.0f : 0.0f;
    if (std::abs(inspector_width_ - target) > 0.5f) {
        inspector_width_ += (target - inspector_width_) * 0.35f;
        RequestLayout();
    } else if (inspector_width_ != target) {
        inspector_width_ = target;
        RequestLayout();
    }
    RebuildInspector();

    // Autosave (debounced ~2 s, gated by the settings preference).
    if (autosave_enabled_ && autosave_dirty_ &&
        std::chrono::steady_clock::now() - last_autosave_ > std::chrono::seconds(2)) {
        FlushAutosave();
    }
}

} // namespace XER::UI::Widgets
