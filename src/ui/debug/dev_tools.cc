#include "dev_tools.h"

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

#include <algorithm>
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
// DevTools::Impl — declared before DevToolsView so the view can call back
// into it; method bodies that touch the view are defined after the view.
// ---------------------------------------------------------------------------

} // namespace

class DevTools::Impl {
public:
    explicit Impl(Core::Window& target) : target_(target.shared_from_this()) {}

    bool Init();
    void Poll();
    void OnWindowClose();

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
    int poll_count_ = 0;
};

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

    // Panel refresh entry points called by DevTools::Impl::Poll().
    void RefreshElements();
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
    window_->SetFrameCallback([this] { Poll(); }, 250);
    window_->Show();
    return true;
}

void DevTools::Impl::Poll() {
    if (!view_) return;
    ++poll_count_;
    auto target = target_.lock();
    if (!target) {
        view_->SetTargetClosed();
        return;
    }
    view_->UpdateRenderPanel(target.get());
    view_->UpdateStatsPanel(target_content().get());
    // Elements auto-refresh ~1s (every 4th 250ms poll).
    if (auto_refresh_ && (poll_count_ % 4) == 0) {
        view_->RefreshElements();
    }
}

void DevTools::Impl::OnWindowClose() {
    // Erase the registry entry; if the caller dropped its reference the
    // DevTools (and its window) is destroyed here.
    auto it = std::find_if(Registry().begin(), Registry().end(),
                           [this](const std::shared_ptr<DevTools>& d) {
                               return d->impl_.get() == this;
                           });
    if (it != Registry().end()) Registry().erase(it);
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
    tabs_->AddTab("Elements");
    tabs_->AddTab("Render");
    tabs_->AddTab("Stats");
    tabs_->SetActiveIndex(0);
    tabs_->SetOnChanged([this](int index) { ShowTab(index); });

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
    AddChild(elem_scroll_);
    AddChild(det_label_);
    AddChild(det_scroll_);
    AddChild(render_scroll_);
    AddChild(stats_scroll_);

    ShowTab(0);
}

void DevToolsView::SetElementsRoot(std::shared_ptr<Widgets::Widget> root) {
    if (elements_view_) {
        elements_view_->SetRoot(std::move(root));
        elements_view_->Refresh();
    }
}

void DevToolsView::ShowTab(int index) {
    const bool elements = index == 0;
    const bool render = index == 1;
    elem_scroll_->SetVisible(elements);
    det_label_->SetVisible(elements);
    det_scroll_->SetVisible(elements);
    render_scroll_->SetVisible(render);
    stats_scroll_->SetVisible(index == 2);
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
    det_label_->SetText(std::format(
        "{} — {:.0f},{:.0f} {:.0f}x{:.0f}", internal::QualifiedName(*w), b.x,
        b.y, b.width, b.height));
    det_view_->SetRoot(w);
    det_view_->Refresh();
}

void DevToolsView::RefreshElements() {
    if (elements_view_) elements_view_->Refresh();
    if (det_view_) det_view_->Refresh();
}

void DevToolsView::SetTargetTitle(const std::string& title) {
    title_label_->SetText("DevTools — " + title);
}

void DevToolsView::SetTargetClosed() {
    if (target_closed_) return;
    target_closed_ = true;
    title_label_->SetText("DevTools — target closed");
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

    const float elem_w = w * 0.58f;
    const float det_x = bounds.x + elem_w + gap * 2.0f;
    const float det_w = w - elem_w - gap * 3.0f;

    elem_scroll_->Layout(Layout::Rect{bounds.x + gap, content_y, elem_w,
                                      content_h});
    det_label_->Layout(Layout::Rect{det_x, content_y, det_w, 22.0f});
    det_scroll_->Layout(Layout::Rect{det_x, content_y + 26.0f, det_w,
                                     content_h - 26.0f});
    render_scroll_->Layout(Layout::Rect{bounds.x + gap, content_y,
                                        w - gap * 2.0f, content_h});
    stats_scroll_->Layout(Layout::Rect{bounds.x + gap, content_y,
                                       w - gap * 2.0f, content_h});
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
    auto impl = std::make_unique<Impl>(target);
    auto devtools = std::shared_ptr<DevTools>(new DevTools(std::move(impl)));
    if (!devtools->impl_->Init()) return nullptr;
    Registry().insert(devtools);
    return devtools;
}

} // namespace XER::UI::Debug
