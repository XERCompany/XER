#include "dev_tools.h"

#include "debug_map_ui.h"
#include "elements_view.h"
#include "debug_internal.h"
#include "../core/window.h"
#include "../rendering/renderer.h"
#include "../theme/theme.h"
#include "../theme/tokens.h"
#include "../widgets/widget.h"
#include "../widgets/button.h"
#include "../widgets/checkbox.h"
#include "../widgets/form.h"
#include "../widgets/label.h"
#include "../widgets/scroll_area.h"
#include "../widgets/tabs.h"
#include "../layout/geometry.h"
#include "helper/string_utils.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace XER::UI::Debug {

namespace Widgets = XER::UI::Widgets;
namespace Layout = XER::UI::Layout;
namespace Theme = XER::UI::Theme;
namespace Core = XER::UI::Core;

namespace {

// ---------------------------------------------------------------------------
// Stats collection
// ---------------------------------------------------------------------------

struct TreeStats {
    int total = 0;
    int visible = 0;
    int shown = 0;
    int enabled = 0;
    int max_depth = 0;
    float content_height = 0.0f;
    std::unordered_map<std::string, int> by_type;
};

void CollectStats(const Widgets::Widget& w, int depth,
                  std::unordered_set<const Widgets::Widget*>& seen,
                  TreeStats& st) {
    if (!seen.insert(&w).second) return;
    ++st.total;
    st.max_depth = std::max(st.max_depth, depth);
    if (w.IsVisible()) ++st.visible;
    if (w.IsEffectivelyVisible()) ++st.shown;
    if (w.IsEnabled()) ++st.enabled;
    st.content_height = std::max(st.content_height, w.GetContentHeight());
    ++st.by_type[internal::QualifiedName(w)];
    for (const auto& c : w.GetChildren()) {
        if (c) CollectStats(*c, depth + 1, seen, st);
    }
}

class DevToolsView;

// ---------------------------------------------------------------------------
// DebugTextView — read-only multi-line text dump (DebugMapUI XML output).
// Like ElementsView it reports full content height and relies on the
// parent ScrollArea for scrolling/clipping; rows outside the visible
// rect are culled in Render().
// ---------------------------------------------------------------------------

class DebugTextView final : public Widgets::Widget {
public:
    DebugTextView() { SetClipsChildren(true); }

    void SetText(std::string text) {
        if (text == text_) return;
        text_ = std::move(text);
        lines_ = XER::Helper::String::SplitLines(text_);
        if (lines_.empty()) lines_.emplace_back("(empty)");
        Invalidate();
    }
    const std::string& GetText() const { return text_; }

    void Render(Rendering::Renderer& renderer,
                const Theme::Tokens& tokens) override {
        const Layout::Rect b = GetBounds();
        if (b.width <= 0.0f || b.height <= 0.0f) return;
        renderer.FillRect(b, tokens.surface, tokens.radius_small);

        const Layout::Rect view = GetVisibleRect();
        const float lh = LineHeight();
        int i0 = static_cast<int>(
            std::floor((view.y - b.y) / lh)) - 1;
        int i1 = static_cast<int>(
            std::ceil((view.y + view.height - b.y) / lh)) + 1;
        const int count = static_cast<int>(lines_.size());
        i0 = std::max(0, i0);
        i1 = std::min(count, i1);

        Rendering::TextStyle style;
        style.size = tokens.font_small;
        style.weight = 400;
        style.color = tokens.foreground;
        Rendering::TextStyle decl_style = style;
        decl_style.color = tokens.muted;

        const float pad = 8.0f;
        for (int i = i0; i < i1; ++i) {
            const std::string& line = lines_[static_cast<std::size_t>(i)];
            // XML prolog / comments render muted; elements stay bright.
            const bool decl = line.find("<?") != std::string::npos ||
                              line.find("<!--") != std::string::npos;
            renderer.DrawText(
                line,
                Layout::Rect{b.x + pad, b.y + static_cast<float>(i) * lh,
                             b.width - 2.0f * pad, lh},
                decl ? decl_style : style);
        }
    }

    void Layout(const Layout::Rect& bounds) override { SetBounds(bounds); }
    using Widget::Layout;
    bool HandleEvent(const Widgets::Event&) override { return false; }

    Layout::Size GetMinSize() const override { return {120.0f, 80.0f}; }
    Layout::Size GetPreferredSize() const override { return {420.0f, 320.0f}; }
    Layout::Size GetMaxSize() const override {
        return {100000.0f, 100000.0f};
    }
    float GetContentHeight() const override {
        return static_cast<float>(lines_.size()) * LineHeight();
    }

private:
    static constexpr float LineHeight() { return 18.0f; }
    std::string text_;
    std::vector<std::string> lines_;
};

// ---------------------------------------------------------------------------
// DevTools::Impl — declared before DevToolsView so the view can call back
// into it; method bodies that touch the view are defined after the view.
// ---------------------------------------------------------------------------

} // namespace

class DevTools::Impl {
public:
    explicit Impl(Core::Window& target) : target_(target.shared_from_this()) {}
    ~Impl();

    bool Init();
    void Poll();
    void OnWindowClose();
    void OnTargetClosing();

    std::shared_ptr<Widgets::Widget> target_content() const {
        if (auto t = target_.lock()) return t->GetContent();
        return nullptr;
    }

    std::string target_title() const {
        if (auto t = target_.lock()) return t->GetTitle();
        return "<closed>";
    }

    void SetAutoRefresh(bool on) { auto_refresh_ = on; }

    std::weak_ptr<Core::Window> target_;
    std::shared_ptr<Core::Window> window_;
    std::shared_ptr<DevToolsView> view_;
    bool auto_refresh_ = true;
    bool close_requested_ = false;
    std::uint64_t close_hook_id_ = 0;
    int poll_count_ = 0;
};

DevTools::Impl::~Impl() {
    // Unregister the target close hook; if the target is already gone
    // its hook table died with it and there is nothing to remove.
    if (close_hook_id_ != 0) {
        if (auto t = target_.lock()) t->RemoveCloseHook(close_hook_id_);
    }
}

namespace {

// ---------------------------------------------------------------------------
// DevToolsView — the whole panel UI as one manually-laid-out widget
// ---------------------------------------------------------------------------

class DevToolsView final : public Widgets::Widget {
public:
    DevToolsView() = default;
    void SetAutoRefreshHandler(std::function<void(bool)> cb) {
        on_auto_change_ = std::move(cb);
    }

    void Build();
    void SetElementsRoot(std::shared_ptr<Widgets::Widget> root);
    // Re-points the Elements tree (and XML dump) when the target window's
    // content widget is replaced; no-op while the root is unchanged.
    void EnsureElementsRoot(std::shared_ptr<Widgets::Widget> root);

    // Panel refresh entry points called by DevTools::Impl::Poll().
    void RefreshElements();
    void UpdateDebugPanel(const Widgets::Widget* root);
    void UpdateRenderPanel(const Core::Window* target);
    void UpdateStatsPanel(const Widgets::Widget* root);
    void SetTargetTitle(const std::string& title);
    void SetTargetClosed();

    void Render(Rendering::Renderer& renderer,
                const Theme::Tokens& tokens) override;
    void Layout(const Layout::Rect& bounds) override;
    void Layout(const Layout::Rect& bounds,
                const Theme::Tokens& tokens) override;
    bool HandleEvent(const Widgets::Event&) override { return false; }

    Layout::Size GetMinSize() const override { return {480.0f, 320.0f}; }
    Layout::Size GetPreferredSize() const override {
        return {960.0f, 640.0f};
    }
    Layout::Size GetMaxSize() const override {
        return {100000.0f, 100000.0f};
    }

private:
    void ShowTab(int index);
    void OnElementsSelect(std::shared_ptr<Widgets::Widget> w);

    std::function<void(bool)> on_auto_change_;

    std::shared_ptr<Widgets::Label> title_label_;
    std::shared_ptr<Widgets::Button> refresh_btn_;
    std::shared_ptr<Widgets::Checkbox> auto_chk_;
    std::shared_ptr<Widgets::Label> auto_label_;
    std::shared_ptr<Widgets::Tabs> tabs_;

    // Debug tab: DebugMapUI XML dump of the whole widget tree.
    std::shared_ptr<Widgets::ScrollArea> debug_scroll_;
    std::shared_ptr<DebugTextView> debug_view_;

    // Elements tab: tree + selected-subtree detail.
    std::shared_ptr<Widgets::ScrollArea> elem_scroll_;
    std::shared_ptr<ElementsView> elements_view_;
    std::shared_ptr<Widgets::Label> det_label_;
    std::shared_ptr<Widgets::ScrollArea> det_scroll_;
    std::shared_ptr<ElementsView> det_view_;

    // Render tab.
    std::shared_ptr<Widgets::ScrollArea> render_scroll_;
    std::shared_ptr<Widgets::Form> render_form_;
    std::vector<std::shared_ptr<Widgets::Label>> render_labels_;

    // Stats tab.
    std::shared_ptr<Widgets::ScrollArea> stats_scroll_;
    std::shared_ptr<Widgets::Form> stats_form_;
    std::vector<std::shared_ptr<Widgets::Label>> stats_labels_;

    bool target_closed_ = false;
};

// Registry keeps DevTools windows alive independent of the caller's
// shared_ptr (browser-DevTools semantics: window lives until closed).
std::unordered_set<std::shared_ptr<DevTools>>& Registry() {
    static std::unordered_set<std::shared_ptr<DevTools>> r;
    return r;
}

// Closed DevTools are moved here instead of being destroyed inside the
// WM_CLOSE/on_close_ callback — dropping the last reference there would
// run ~Window re-entrantly inside its own WndProc. Entries are released
// on the next Inspect() call (user thread, outside message handling) or
// at process exit.
std::vector<std::shared_ptr<DevTools>>& Graveyard() {
    static std::vector<std::shared_ptr<DevTools>> g;
    return g;
}

} // namespace

// ---------------------------------------------------------------------------
// DevTools::Impl method bodies (need the complete DevToolsView)
// ---------------------------------------------------------------------------

bool DevTools::Impl::Init() {
    auto content = target_content();
    if (!content) return false;

    window_ = std::make_shared<Core::Window>();
    if (!window_->Create("DevTools", 980, 640)) return false;

    view_ = std::make_shared<DevToolsView>();
    view_->SetAutoRefreshHandler(
        [this](bool on) { auto_refresh_ = on; });
    view_->Build();
    view_->SetTargetTitle(target_title());
    view_->SetElementsRoot(content);
    window_->SetContent(view_);
    window_->SetOnClose([this] { OnWindowClose(); });
    // Synchronously close the DevTools window when the inspected window
    // starts closing — covers WM_CLOSE and ~Window teardown alike, so a
    // dead frame callback can never orphan this window.
    if (auto t = target_.lock()) {
        close_hook_id_ = t->AddCloseHook([this] { OnTargetClosing(); });
    }
    window_->SetFrameCallback([this] { Poll(); }, 250);
    window_->Show();
    return true;
}

void DevTools::Impl::Poll() {
    if (!view_) return;
    ++poll_count_;
    auto target = target_.lock();
    if (!target || target->IsClosed()) {
        OnTargetClosing();
        return;
    }
    auto content = target_content();
    view_->EnsureElementsRoot(content);
    view_->UpdateRenderPanel(target.get());
    view_->UpdateStatsPanel(content.get());
    // Elements + XML auto-refresh ~1s (every 4th 250ms poll).
    if (auto_refresh_ && (poll_count_ % 4) == 0) {
        view_->RefreshElements();
    }
}

void DevTools::Impl::OnTargetClosing() {
    // Close() only posts WM_CLOSE; destruction happens later in
    // OnWindowClose via the window's own message handling.
    if (window_ && !close_requested_ && !window_->IsClosed()) {
        close_requested_ = true;
        window_->Close();
    }
}

void DevTools::Impl::OnWindowClose() {
    // Move the registry entry to the graveyard; actual destruction is
    // deferred to the next Inspect() call — never inside WM_CLOSE.
    auto it = std::find_if(Registry().begin(), Registry().end(),
                           [this](const std::shared_ptr<DevTools>& d) {
                               return d->impl_.get() == this;
                           });
    if (it != Registry().end()) {
        Graveyard().push_back(*it);
        Registry().erase(it);
    }
}

// ---------------------------------------------------------------------------
// DevToolsView implementation
// ---------------------------------------------------------------------------

void DevToolsView::Build() {
    title_label_ = std::make_shared<Widgets::Label>("DevTools");
    title_label_->SetTooltip("Widget tree inspector");

    refresh_btn_ = std::make_shared<Widgets::Button>("Refresh");
    refresh_btn_->SetOnClick([this] { RefreshElements(); });

    auto_chk_ = std::make_shared<Widgets::Checkbox>(true);
    auto_chk_->SetTooltip("Live-refresh the elements tree");
    auto_chk_->SetOnChanged([this](bool on) {
        if (on_auto_change_) on_auto_change_(on);
    });
    auto_label_ = std::make_shared<Widgets::Label>("auto", true);

    tabs_ = std::make_shared<Widgets::Tabs>();
    tabs_->AddTab("Debug");
    tabs_->AddTab("Elements");
    tabs_->AddTab("Render");
    tabs_->AddTab("Stats");
    tabs_->SetActiveIndex(1);
    tabs_->SetOnChanged([this](int index) { ShowTab(index); });

    debug_view_ = std::make_shared<DebugTextView>();
    debug_scroll_ = std::make_shared<Widgets::ScrollArea>(debug_view_);

    elements_view_ = std::make_shared<ElementsView>();
    elements_view_->SetOnSelect(
        [this](std::shared_ptr<Widgets::Widget> w) { OnElementsSelect(w); });
    elem_scroll_ = std::make_shared<Widgets::ScrollArea>(elements_view_);

    det_label_ = std::make_shared<Widgets::Label>("No selection", true);
    det_view_ = std::make_shared<ElementsView>();
    det_scroll_ = std::make_shared<Widgets::ScrollArea>(det_view_);

    render_form_ = std::make_shared<Widgets::Form>(1);
    for (int i = 0; i < 9; ++i) {
        auto l = std::make_shared<Widgets::Label>("", true);
        render_labels_.push_back(l);
        render_form_->AddChild(l);
    }
    render_scroll_ = std::make_shared<Widgets::ScrollArea>(render_form_);

    stats_form_ = std::make_shared<Widgets::Form>(1);
    for (int i = 0; i < 14; ++i) {
        auto l = std::make_shared<Widgets::Label>("", true);
        stats_labels_.push_back(l);
        stats_form_->AddChild(l);
    }
    stats_scroll_ = std::make_shared<Widgets::ScrollArea>(stats_form_);

    AddChild(title_label_);
    AddChild(refresh_btn_);
    AddChild(auto_chk_);
    AddChild(auto_label_);
    AddChild(tabs_);
    AddChild(debug_scroll_);
    AddChild(elem_scroll_);
    AddChild(det_label_);
    AddChild(det_scroll_);
    AddChild(render_scroll_);
    AddChild(stats_scroll_);

    ShowTab(1);
}

void DevToolsView::SetElementsRoot(std::shared_ptr<Widgets::Widget> root) {
    if (elements_view_) {
        elements_view_->SetRoot(std::move(root));
        elements_view_->Refresh();
        UpdateDebugPanel(elements_view_->GetRoot().get());
    }
}

void DevToolsView::EnsureElementsRoot(std::shared_ptr<Widgets::Widget> root) {
    if (!elements_view_) return;
    if (elements_view_->GetRoot().get() ==
        (root ? root.get() : nullptr)) {
        return;
    }
    SetElementsRoot(std::move(root));
}

void DevToolsView::UpdateDebugPanel(const Widgets::Widget* root) {
    if (!debug_view_) return;
    debug_view_->SetText(root ? DebugMapUI::Dump(root)
                              : std::string("<DebugMapUI nodes=\"0\"/>"));
}

void DevToolsView::ShowTab(int index) {
    const bool debug = index == 0;
    const bool elements = index == 1;
    const bool render = index == 2;
    debug_scroll_->SetVisible(debug);
    elem_scroll_->SetVisible(elements);
    det_label_->SetVisible(elements);
    det_scroll_->SetVisible(elements);
    render_scroll_->SetVisible(render);
    stats_scroll_->SetVisible(index == 3);
    Invalidate();
}

void DevToolsView::OnElementsSelect(std::shared_ptr<Widgets::Widget> w) {
    if (!w) {
        det_label_->SetText("No selection");
        det_view_->SetRoot(nullptr);
        det_view_->Refresh();
        return;
    }
    const auto b = w->GetBounds();
    std::string info = std::format("{} — {:.0f},{:.0f} {:.0f}x{:.0f}",
                                   internal::QualifiedName(*w), b.x, b.y,
                                   b.width, b.height);
    const std::string dt = w->GetDebugText();
    if (!dt.empty()) {
        constexpr std::size_t kMax = 72;
        info += "  \"" +
                (dt.size() > kMax ? dt.substr(0, kMax - 3) + "..." : dt) +
                "\"";
    }
    det_label_->SetText(std::move(info));
    det_view_->SetRoot(w);
    det_view_->Refresh();
}

void DevToolsView::RefreshElements() {
    if (elements_view_) elements_view_->Refresh();
    if (det_view_) det_view_->Refresh();
    UpdateDebugPanel(elements_view_ ? elements_view_->GetRoot().get()
                                    : nullptr);
}

void DevToolsView::SetTargetTitle(const std::string& title) {
    title_label_->SetText("DevTools — " + title);
}

void DevToolsView::SetTargetClosed() {
    if (target_closed_) return;
    target_closed_ = true;
    title_label_->SetText("DevTools — target closed");
    if (debug_view_) debug_view_->SetText("");
    render_labels_[0]->SetText("target window is gone");
    for (std::size_t i = 1; i < render_labels_.size(); ++i) {
        render_labels_[i]->SetText("");
    }
    for (auto& l : stats_labels_) l->SetText("");
}

void DevToolsView::UpdateRenderPanel(const Core::Window* target) {
    if (target_closed_ || !target) return;
    const Core::RenderStats s = target->GetRenderStats();
    render_labels_[0]->SetText(std::format("frames: {}", s.frames));
    render_labels_[1]->SetText(
        std::format("last frame: {:.2f} ms", s.last_frame_ms));
    render_labels_[2]->SetText(
        std::format("avg frame:  {:.2f} ms", s.avg_frame_ms));
    render_labels_[3]->SetText(
        std::format("damage:     {:.1f}%{}", s.last_damage_pct,
                    s.last_frame_full ? " (full)" : ""));
    int w = 0, h = 0;
    const_cast<Core::Window*>(target)->GetSize(&w, &h);
    render_labels_[4]->SetText(std::format("size:       {}x{}", w, h));
    const std::string err = target->GetLastRenderError();
    render_labels_[5]->SetText(
        err.empty() ? "render:     ok" : "render:     " + err);
    const auto content = target->GetContent();
    render_labels_[6]->SetText(
        content ? std::format("root:       {}",
                              internal::QualifiedName(*content))
                : "root:       (none)");
    render_labels_[7]->SetText(
        std::format("theme:      {}",
                    target->GetTheme() == Theme::Variant::Dark ? "dark"
                                                              : "light"));
    render_labels_[8]->SetText("");
}

void DevToolsView::UpdateStatsPanel(const Widgets::Widget* root) {
    if (target_closed_) return;
    if (!root) {
        stats_labels_[0]->SetText("no content widget");
        return;
    }
    TreeStats st;
    std::unordered_set<const Widgets::Widget*> seen;
    CollectStats(*root, 0, seen, st);

    stats_labels_[0]->SetText(std::format("widgets:    {}", st.total));
    stats_labels_[1]->SetText(
        std::format("visible:    {}  shown: {}", st.visible, st.shown));
    stats_labels_[2]->SetText(std::format("enabled:    {}", st.enabled));
    stats_labels_[3]->SetText(std::format("max depth:  {}", st.max_depth));
    stats_labels_[4]->SetText(
        std::format("content h:  {:.0f}", st.content_height));

    // Top widget types by count.
    std::vector<std::pair<std::string, int>> types(st.by_type.begin(),
                                                   st.by_type.end());
    std::sort(types.begin(), types.end(),
              [](const auto& a, const auto& b) {
                  return a.second > b.second;
              });
    int row = 5;
    const int cap = std::min<int>(9, static_cast<int>(types.size()));
    for (int i = 0; i < cap && row < 14; ++i, ++row) {
        stats_labels_[static_cast<std::size_t>(row)]->SetText(
            std::format("{:>4}  {}",
                        types[static_cast<std::size_t>(i)].second,
                        types[static_cast<std::size_t>(i)].first));
    }
    for (; row < 14; ++row) {
        stats_labels_[static_cast<std::size_t>(row)]->SetText("");
    }
}

void DevToolsView::Render(Rendering::Renderer& renderer,
                          const Theme::Tokens& tokens) {
    const Layout::Rect b = GetBounds();
    renderer.FillRect(b, tokens.background, 0.0f);
}

void DevToolsView::Layout(const Layout::Rect& bounds) {
    SetBounds(bounds);
    const Theme::Tokens& t = CachedLayoutTokens();

    const float header_h = 34.0f;
    const float tabs_h = 30.0f;
    const float gap = t.spacing_small;
    const float content_y = bounds.y + header_h + tabs_h + gap;
    const float content_h = bounds.height - header_h - tabs_h - gap * 2.0f;
    const float w = bounds.width;

    title_label_->Layout(Layout::Rect{bounds.x + gap, bounds.y + 8.0f,
                                      w * 0.45f, header_h - 12.0f});
    refresh_btn_->Layout(Layout::Rect{bounds.x + w - 76.0f, bounds.y + 5.0f,
                                      70.0f, 24.0f});
    auto_chk_->Layout(Layout::Rect{bounds.x + w - 170.0f, bounds.y + 8.0f,
                                   22.0f, 18.0f});
    auto_label_->Layout(Layout::Rect{bounds.x + w - 144.0f, bounds.y + 7.0f,
                                     60.0f, 18.0f});
    tabs_->Layout(Layout::Rect{bounds.x + gap, bounds.y + header_h,
                               w - gap * 2.0f, tabs_h});

    const Layout::Rect content_rect{bounds.x + gap, content_y,
                                    w - gap * 2.0f, content_h};
    debug_scroll_->Layout(content_rect);
    render_scroll_->Layout(content_rect);
    stats_scroll_->Layout(content_rect);

    const float elem_w = w * 0.58f;
    const float det_x = bounds.x + elem_w + gap * 2.0f;
    const float det_w = w - elem_w - gap * 3.0f;

    elem_scroll_->Layout(Layout::Rect{bounds.x + gap, content_y, elem_w,
                                      content_h});
    det_label_->Layout(Layout::Rect{det_x, content_y, det_w, 22.0f});
    det_scroll_->Layout(Layout::Rect{det_x, content_y + 26.0f, det_w,
                                     content_h - 26.0f});
}

void DevToolsView::Layout(const Layout::Rect& bounds,
                          const Theme::Tokens& tokens) {
    CacheLayoutTokens(tokens);
    this->Layout(bounds);
}

// ---------------------------------------------------------------------------
// DevTools
// ---------------------------------------------------------------------------

DevTools::DevTools(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

DevTools::~DevTools() = default;

Core::Window& DevTools::GetWindow() const { return *impl_->window_; }

std::shared_ptr<DevTools> DevTools::Inspect(Core::Window& target) {
    if (!target.GetContent()) return nullptr;
    // Release graveyard entries from previously closed DevTools windows —
    // safe here because Inspect runs on the caller's thread, never inside
    // a window message callback.
    Graveyard().clear();
    auto impl = std::make_unique<Impl>(target);
    auto devtools = std::shared_ptr<DevTools>(new DevTools(std::move(impl)));
    if (!devtools->impl_->Init()) return nullptr;
    Registry().insert(devtools);
    return devtools;
}

} // namespace XER::UI::Debug
