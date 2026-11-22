#include "shell_widgets.h"

#include <algorithm>
#include <utility>

#include "rendering/renderer.h"
#include "theme/tokens.h"
#include "widgets/surface.h"

#include "ve_theme.h"

namespace XER::VideoEditor::Ui2 {
namespace {

float MainExtent(const Layout::Size& s, LinearBox::Direction d) {
    return d == LinearBox::Direction::Horizontal ? s.width : s.height;
}

}  // namespace

// ── LinearBox ────────────────────────────────────────────────────────

std::shared_ptr<LinearBox> LinearBox::Create(Direction dir) {
    return std::shared_ptr<LinearBox>(new LinearBox(dir));
}

void LinearBox::SetDirection(Direction dir) {
    dir_ = dir;
    RequestLayout();
}

void LinearBox::SetSpacing(float px) {
    spacing_ = std::max(0.0f, px);
    RequestLayout();
}

void LinearBox::SetPadding(Layout::EdgeInsets insets) {
    padding_ = insets;
    RequestLayout();
}

void LinearBox::Add(std::shared_ptr<Widgets::Widget> child, Slot slot) {
    if (child == nullptr) return;
    AddChild(std::move(child));
    slots_.push_back(slot);
    RequestLayout();
}

void LinearBox::Clear() {
    // RemoveChild mutates children_ — drain from the front.
    while (!GetChildren().empty()) {
        RemoveChild(GetChildren().front());
    }
    slots_.clear();
    RequestLayout();
}

void LinearBox::Render(Rendering::Renderer&,
                       const Theme::Tokens&) {
    // Pure container — children paint themselves via RenderSubtree.
}

void LinearBox::Layout(const Layout::Rect& bounds) {
    SetBounds(bounds);
    const auto& children = GetChildren();
    while (slots_.size() < children.size()) slots_.push_back({});
    if (slots_.size() > children.size()) slots_.resize(children.size());

    const Layout::Rect content = padding_.Apply(bounds);
    const bool horiz = dir_ == Direction::Horizontal;
    int visible = 0;
    for (const auto& c : children) {
        if (c->IsVisible()) ++visible;
    }
    const float gaps = spacing_ * static_cast<float>(
        visible > 0 ? visible - 1 : 0);
    const float main_span =
        std::max(0.0f, (horiz ? content.width : content.height) - gaps);

    float fixed_sum = 0.0f;
    float flex_sum = 0.0f;
    for (std::size_t i = 0; i < children.size(); ++i) {
        if (!children[i]->IsVisible()) continue;
        const Slot& s = slots_[i];
        if (s.fixed > 0.0f) {
            fixed_sum += s.fixed;
        } else if (s.flex > 0.0f) {
            flex_sum += s.flex;
        } else {
            fixed_sum += MainExtent(children[i]->GetPreferredSize(), dir_);
        }
    }
    const float remaining = std::max(0.0f, main_span - fixed_sum);

    float cursor = horiz ? content.x : content.y;
    for (std::size_t i = 0; i < children.size(); ++i) {
        const auto& child = children[i];
        if (!child->IsVisible()) continue;
        const Slot& s = slots_[i];
        float extent;
        if (s.fixed > 0.0f) {
            extent = s.fixed;
        } else if (s.flex > 0.0f) {
            extent = flex_sum > 0.0f ? remaining * (s.flex / flex_sum)
                                     : 0.0f;
        } else {
            extent = MainExtent(child->GetPreferredSize(), dir_);
        }
        Layout::Rect r;
        if (horiz) {
            r = {cursor, content.y, extent, content.height};
        } else {
            r = {content.x, cursor, content.width, extent};
        }
        // SetBounds resolves margin/fixed-size/alignment centrally —
        // containers just assign the slot (widget.h layout contract).
        child->Layout(r, CachedLayoutTokens());
        child->SetBounds(r);
        cursor += extent + spacing_;
    }
}

Layout::Size LinearBox::GetMinSize() const {
    return {0.0f, 0.0f};
}

Layout::Size LinearBox::GetPreferredSize() const {
    float main = 0.0f;
    float cross = 0.0f;
    int visible = 0;
    const auto& children = GetChildren();
    for (std::size_t i = 0; i < children.size(); ++i) {
        if (!children[i]->IsVisible()) continue;
        ++visible;
        const Layout::Size p = children[i]->GetPreferredSize();
        main += MainExtent(p, dir_);
        cross = std::max(cross, dir_ == Direction::Horizontal
                                    ? p.height : p.width);
    }
    main += spacing_ * static_cast<float>(visible > 0 ? visible - 1 : 0);
    main += dir_ == Direction::Horizontal ? padding_.Horizontal()
                                          : padding_.Vertical();
    cross += dir_ == Direction::Horizontal ? padding_.Vertical()
                                           : padding_.Horizontal();
    return dir_ == Direction::Horizontal ? Layout::Size(main, cross)
                                         : Layout::Size(cross, main);
}

Layout::Size LinearBox::GetMaxSize() const {
    return {100000.0f, 100000.0f};
}

// ── DockTabGroup ─────────────────────────────────────────────────────

std::shared_ptr<DockTabGroup> DockTabGroup::Create() {
    auto g = std::shared_ptr<DockTabGroup>(new DockTabGroup());
    g->tabs_ = std::make_shared<Widgets::Tabs>();
    g->tabs_->SetStyle(Widgets::Tabs::Style::Underline);
    g->AddChild(g->tabs_);
    // Raw capture — a shared_ptr here would cycle g -> tabs_ -> this.
    DockTabGroup* self = g.get();
    g->tabs_->SetOnChanged([self](int index) {
        self->SyncVisible();
        if (self->on_changed_ != nullptr) self->on_changed_(index);
        self->Invalidate();
    });
    return g;
}

void DockTabGroup::AddTab(std::string label, std::string tooltip,
                          std::shared_ptr<Widgets::Widget> panel) {
    tabs_->AddTab(std::move(label), std::move(tooltip));
    if (panel != nullptr) {
        panel->SetVisible(false);
        AddChild(panel);
    }
    panels_.push_back(std::move(panel));
    if (panels_.size() == 1u) SyncVisible();
    RequestLayout();
}

int DockTabGroup::ActiveIndex() const {
    return tabs_ != nullptr ? tabs_->GetActiveIndex() : -1;
}

void DockTabGroup::SetActiveIndex(int index) {
    if (tabs_ != nullptr) tabs_->SetActiveIndex(index);
    SyncVisible();
}

std::shared_ptr<Widgets::Widget> DockTabGroup::PanelAt(int index) const {
    if (index < 0 || index >= static_cast<int>(panels_.size())) {
        return nullptr;
    }
    return panels_[static_cast<std::size_t>(index)];
}

void DockTabGroup::SetOnChanged(std::function<void(int)> cb) {
    on_changed_ = std::move(cb);
}

void DockTabGroup::SyncVisible() {
    const int active = ActiveIndex();
    for (std::size_t i = 0; i < panels_.size(); ++i) {
        if (panels_[i] != nullptr) {
            panels_[i]->SetVisible(static_cast<int>(i) == active);
        }
    }
    Invalidate();
    RequestLayout();
}

void DockTabGroup::Render(Rendering::Renderer& renderer,
                          const Theme::Tokens& tokens) {
    const Layout::Rect b = GetBounds();
    renderer.FillRect(b, tokens.panel, 0.0f);
    const Layout::Rect strip{b.x, b.y, b.width, strip_height_};
    renderer.DrawLine({b.x, strip.y + strip_height_ - 0.5f},
                      {b.x + b.width, strip.y + strip_height_ - 0.5f},
                      tokens.border, 1.0f);
}

void DockTabGroup::Layout(const Layout::Rect& bounds) {
    SetBounds(bounds);
    if (tabs_ != nullptr) {
        const Layout::Rect strip{bounds.x, bounds.y, bounds.width,
                                 strip_height_};
        tabs_->Layout(strip, CachedLayoutTokens());
        tabs_->SetBounds(strip);
    }
    const Layout::Rect content{bounds.x, bounds.y + strip_height_,
                               bounds.width,
                               std::max(0.0f,
                                        bounds.height - strip_height_)};
    const int active = ActiveIndex();
    for (std::size_t i = 0; i < panels_.size(); ++i) {
        if (panels_[i] == nullptr) continue;
        if (static_cast<int>(i) == active) {
            panels_[i]->Layout(content, CachedLayoutTokens());
            panels_[i]->SetBounds(content);
        } else {
            panels_[i]->SetBounds({});
        }
    }
}

Layout::Size DockTabGroup::GetMinSize() const {
    return {120.0f, strip_height_ + 40.0f};
}

Layout::Size DockTabGroup::GetPreferredSize() const {
    return {280.0f, 360.0f};
}

Layout::Size DockTabGroup::GetMaxSize() const {
    return {100000.0f, 100000.0f};
}

// ── PlaceholderPanel ─────────────────────────────────────────────────

std::shared_ptr<PlaceholderPanel> PlaceholderPanel::Create(
    std::string panel_id, std::string title, std::string description) {
    return std::shared_ptr<PlaceholderPanel>(new PlaceholderPanel(
        std::move(panel_id), std::move(title), std::move(description)));
}

PlaceholderPanel::PlaceholderPanel(std::string panel_id,
                                   std::string title,
                                   std::string description)
    : panel_id_(std::move(panel_id)),
      title_(std::move(title)),
      description_(std::move(description)) {}

void PlaceholderPanel::SetDescription(std::string text) {
    description_ = std::move(text);
    Invalidate();
}

void PlaceholderPanel::Render(Rendering::Renderer& renderer,
                              const Theme::Tokens& tokens) {
    const Layout::Rect b = GetBounds();
    renderer.FillRect(b, tokens.surface, 0.0f);
    const float pad = Theme::kSpaceMd;
    Layout::Rect title_rect{b.x + pad, b.y + pad,
                            b.width - pad * 2.0f, 20.0f};
    renderer.DrawText(title_, title_rect,
                      {tokens.foreground, Theme::kFontBody, 600});
    if (!description_.empty()) {
        Layout::Rect desc{b.x + pad, title_rect.y + 24.0f,
                          b.width - pad * 2.0f, 16.0f};
        renderer.DrawText(description_, desc,
                          {tokens.muted, Theme::kFontCaption});
    }
    Layout::Rect foot{b.x + pad, b.y + b.height - pad - 14.0f,
                      b.width - pad * 2.0f, 14.0f};
    renderer.DrawText("M1 placeholder — surface lands in M2/M3", foot,
                      {tokens.disabled, Theme::kFontCaption});
}

void PlaceholderPanel::Layout(const Layout::Rect& bounds) {
    SetBounds(bounds);
}

Layout::Size PlaceholderPanel::GetMinSize() const {
    return {80.0f, 60.0f};
}

Layout::Size PlaceholderPanel::GetPreferredSize() const {
    return {240.0f, 200.0f};
}

Layout::Size PlaceholderPanel::GetMaxSize() const {
    return {100000.0f, 100000.0f};
}

// ── StatusStrip ──────────────────────────────────────────────────────

std::shared_ptr<StatusStrip> StatusStrip::Create() {
    return std::shared_ptr<StatusStrip>(new StatusStrip());
}

void StatusStrip::SetStatusText(std::string text) {
    status_text_ = std::move(text);
    Invalidate();
}

void StatusStrip::SetBadges(std::vector<Badge> badges) {
    badges_ = std::move(badges);
    Invalidate();
}

void StatusStrip::Render(Rendering::Renderer& renderer,
                         const Theme::Tokens& tokens) {
    const Layout::Rect b = GetBounds();
    renderer.FillRect(b, tokens.panel, 0.0f);
    renderer.DrawLine({b.x, b.y + 0.5f}, {b.x + b.width, b.y + 0.5f},
                      tokens.border, 1.0f);
    const float pad = Theme::kSpaceMd;
    Layout::Rect status{b.x + pad, b.y, b.width * 0.5f, b.height};
    renderer.DrawText(status_text_, status,
                      {tokens.muted, Theme::kFontCaption});
    // Right-aligned badge chips.
    float x = b.x + b.width - pad;
    for (auto it = badges_.rbegin(); it != badges_.rend(); ++it) {
        const float w = renderer.MeasureText(
                            it->text, {tokens.muted, Theme::kFontCaption})
                            .width +
                        14.0f;
        x -= w;
        const Layout::Rect chip{x, b.y + 4.0f, w, b.height - 8.0f};
        const Theme::Color fill = it->warning
                                      ? Theme::WithAlpha(
                                            tokens.warning_chip, 0.18f)
                                      : tokens.secondary;
        const Theme::Color fg =
            it->warning ? tokens.warning_chip : tokens.muted;
        renderer.FillRect(chip, fill, Theme::kRadiusSm);
        renderer.DrawText(it->text, chip, {fg, Theme::kFontCaption});
        x -= Theme::kSpaceXs;
        if (x < b.x + b.width * 0.5f) break;
    }
}

void StatusStrip::Layout(const Layout::Rect& bounds) {
    SetBounds(bounds);
}

Layout::Size StatusStrip::GetMinSize() const {
    return {200.0f, 22.0f};
}

Layout::Size StatusStrip::GetPreferredSize() const {
    return {800.0f, Theme::kStatusStripHeight};
}

Layout::Size StatusStrip::GetMaxSize() const {
    return {100000.0f, Theme::kStatusStripHeight};
}

}  // namespace XER::VideoEditor::Ui2
