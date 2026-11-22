#include "card.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "rendering/renderer.h"
#include "theme/tokens.h"

namespace XER::UI::Widgets {
namespace {

constexpr float kMinimumDimension = 32.0f;
constexpr float kPi = 3.14159265358979323846f;
// Size-query paddings mirror the spacing tokens used during layout
// (spacing_large ~24, spacing_medium ~12) — matching the pattern the
// original Card established.
constexpr float kPaddingComfortable = 24.0f;
constexpr float kPaddingCompact = 12.0f;
constexpr float kRegionGap = 10.0f;       // between header/content/footer
constexpr float kChevronSize = 24.0f;
constexpr float kTitleHeight = 20.0f;
constexpr float kDescriptionHeight = 16.0f;
constexpr float kTitleDescGap = 4.0f;

} // namespace

Card::Card() {
    press_.Snap(0.0f);
    hover_t_.Set(0.0f);
    // Rest fully expanded — Set() would animate 0->1 on the first tick.
    collapse_t_.from = 1.0f;
    collapse_t_.to = 1.0f;
}

void Card::SetTitle(std::string title) {
    if (title_ == title) {
        return;
    }
    title_ = std::move(title);
    Invalidate();
    RequestLayout();
}

void Card::SetDescription(std::string description) {
    if (description_ == description) {
        return;
    }
    description_ = std::move(description);
    Invalidate();
    RequestLayout();
}

void Card::SetHeader(std::shared_ptr<Widget> header) {
    if (header_ != nullptr) {
        RemoveChild(header_);
    }
    header_ = std::move(header);
    if (header_ != nullptr) {
        AddChild(header_);
    }
    Invalidate();
    RequestLayout();
}

void Card::SetContent(std::shared_ptr<Widget> content) {
    if (content_ != nullptr) {
        RemoveChild(content_);
    }
    content_ = std::move(content);
    if (content_ != nullptr) {
        AddChild(content_);
    }
    Invalidate();
    RequestLayout();
}

void Card::SetFooter(std::shared_ptr<Widget> footer) {
    if (footer_ != nullptr) {
        RemoveChild(footer_);
    }
    footer_ = std::move(footer);
    if (footer_ != nullptr) {
        AddChild(footer_);
    }
    Invalidate();
    RequestLayout();
}

void Card::SetMediaImage(std::string path) {
    if (media_image_ == path) {
        return;
    }
    media_image_ = std::move(path);
    Invalidate();
    RequestLayout();
}

void Card::SetMediaHeight(float height) {
    media_height_ = std::max(0.0f, height);
    Invalidate();
    RequestLayout();
}

void Card::SetDensity(Density density) {
    if (density_ == density) {
        return;
    }
    density_ = density;
    Invalidate();
    RequestLayout();
}

void Card::SetSelected(bool selected) {
    if (selected_ == selected) {
        return;
    }
    selected_ = selected;
    Invalidate();
}

void Card::SetClickable(bool clickable) {
    if (clickable_ == clickable) {
        return;
    }
    clickable_ = clickable;
    SetFocusable(clickable_ || collapsible_);
    Invalidate();
}

void Card::SetOnClick(std::function<void()> callback) {
    on_click_ = std::move(callback);
    if (on_click_ && !clickable_) {
        SetClickable(true);
    }
}

void Card::SetCollapsible(bool collapsible) {
    if (collapsible_ == collapsible) {
        return;
    }
    collapsible_ = collapsible;
    SetFocusable(clickable_ || collapsible_);
    // Collapsing clips the content area to the animated card bounds.
    SetClipsChildren(collapsible_);
    Invalidate();
    RequestLayout();
}

void Card::SetCollapsed(bool collapsed) {
    if (collapsed_ == collapsed) {
        return;
    }
    collapsed_ = collapsed;
    collapse_t_.Set(collapsed_ ? 0.0f : 1.0f, 0.22f,
                    &Motion::EaseInOutCubic);
    if (on_collapsed_changed_) {
        on_collapsed_changed_(collapsed_);
    }
    Invalidate();
    RequestLayout();
}

void Card::SetOnCollapsedChanged(std::function<void(bool)> callback) {
    on_collapsed_changed_ = std::move(callback);
}

float Card::PaddingFor(const Theme::Tokens& tokens) const {
    return density_ == Density::Compact ? tokens.spacing_medium
                                        : tokens.spacing_large;
}

float Card::HeaderHeight() const {
    if (header_ != nullptr) {
        return header_->GetPreferredSize().height;
    }
    float h = 0.0f;
    if (!title_.empty()) {
        h += kTitleHeight;
    }
    if (!description_.empty()) {
        h += (h > 0.0f ? kTitleDescGap : 0.0f) + kDescriptionHeight;
    }
    // The collapse chevron needs a header strip even with no text.
    if (collapsible_ && h < kChevronSize) {
        h = kChevronSize;
    }
    return h;
}

float Card::FooterHeight() const {
    return footer_ != nullptr ? footer_->GetPreferredSize().height : 0.0f;
}

void Card::Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) {
    const auto bounds = GetBounds();
    const float radius = tokens.radius_large;
    const float press = press_.pos;
    const float hov = hover_t_.Value();

    renderer.FillRect(bounds, tokens.surface, radius);
    renderer.StrokeRect(bounds, tokens.border, 1.0f, radius);

    // Clickable affordances: a quiet hover wash plus a faint press shade
    // (same recipe as Button, but the card surface itself stays put).
    if (clickable_ && IsEnabled()) {
        if (hov > 0.01f) {
            Theme::Color wash = tokens.hover;
            wash.a *= hov * 0.6f;
            renderer.FillRect(bounds, wash, radius);
        }
        if (press > 0.01f) {
            Theme::Color shade = tokens.foreground;
            shade.a = 0.05f * press;
            renderer.FillRect(bounds, shade, radius);
        }
    }

    // Selection ring: accent ring just inside the border (Qt-style
    // selection outline rather than a filled selection tint).
    if (selected_) {
        renderer.StrokeRect(
            Layout::Rect{bounds.x + 1.5f, bounds.y + 1.5f,
                         std::max(0.0f, bounds.width - 3.0f),
                         std::max(0.0f, bounds.height - 3.0f)},
            tokens.focus, 1.5f, std::max(0.0f, radius - 1.5f));
    }

    const float pad = PaddingFor(tokens);
    const float media_h = media_image_.empty() ? 0.0f : media_height_;

    // Media band: edge-to-edge at the card top, corners rounded to the
    // card radius (DrawImage caches the decoded bitmap by path).
    if (media_h > 0.0f) {
        renderer.DrawImage(
            media_image_,
            Layout::Rect{bounds.x, bounds.y, bounds.width,
                         std::min(media_h, bounds.height)},
            1.0f, radius);
    }

    // Text header (widget headers paint themselves via the tree).
    const float header_x = bounds.x + pad;
    const float header_w = std::max(
        0.0f, bounds.width - 2.0f * pad -
                  (collapsible_ ? kChevronSize + kRegionGap : 0.0f));
    float text_y = bounds.y + media_h + pad;
    if (header_ == nullptr) {
        if (!title_.empty()) {
            const Rendering::TextStyle ts{tokens.foreground, 16.0f, 600};
            renderer.DrawText(
                title_, Layout::Rect{header_x, text_y, header_w,
                                     kTitleHeight},
                ts);
            text_y += kTitleHeight + kTitleDescGap;
        }
        if (!description_.empty()) {
            const Rendering::TextStyle ds{tokens.muted,
                                          tokens.font_small, 400};
            renderer.DrawText(
                description_,
                Layout::Rect{header_x, text_y, header_w,
                             kDescriptionHeight},
                ds);
        }
    }

    // Collapse chevron: rotates from down (expanded) to right
    // (collapsed) with the progress value.
    if (collapsible_) {
        const float cx = chevron_rect_.x + chevron_rect_.width * 0.5f;
        const float cy = chevron_rect_.y + chevron_rect_.height * 0.5f;
        const float angle =
            (1.0f - collapse_t_.Value()) * (-kPi * 0.5f);
        const float ca = std::cos(angle), sa = std::sin(angle);
        auto rot = [&](float dx, float dy) {
            return Layout::Point{cx + dx * ca - dy * sa,
                                 cy + dx * sa + dy * ca};
        };
        const Theme::Color c = tokens.muted;
        constexpr float kHalf = 4.0f;
        renderer.DrawLine(rot(-kHalf, -1.5f), rot(0.0f, 2.0f), c, 1.4f);
        renderer.DrawLine(rot(0.0f, 2.0f), rot(kHalf, -1.5f), c, 1.4f);
    }

    RenderFocusRing(renderer, tokens, radius);
}

void Card::Layout(const Layout::Rect& bounds) {
    Layout(bounds, CachedLayoutTokens());
}

void Card::Layout(const Layout::Rect& bounds, const Theme::Tokens& tokens) {
    CacheLayoutTokens(tokens);
    SetBounds(bounds);

    const float pad = PaddingFor(tokens);
    const float media_h = media_image_.empty() ? 0.0f : media_height_;
    const float progress = collapse_t_.Value();

    const float inner_x = bounds.x + pad;
    const float inner_w = std::max(0.0f, bounds.width - 2.0f * pad);
    const float top = bounds.y + media_h + pad;

    const float header_h = HeaderHeight();
    const float footer_h = FooterHeight();

    const float footer_y = bounds.y + bounds.height - pad - footer_h;
    float content_top = top + header_h;
    if (header_h > 0.0f) {
        content_top += kRegionGap;
    }
    float content_bottom = footer_y;
    if (footer_h > 0.0f) {
        content_bottom -= kRegionGap;
    }
    const float content_h =
        std::max(0.0f, content_bottom - content_top) * progress;
    const Layout::Rect content_rect{inner_x, content_top, inner_w,
                                    content_h};

    // Slot children first — they render/hit-test through the tree like
    // any child, the card only positions them.
    if (header_ != nullptr) {
        header_->Layout(
            Layout::Rect{inner_x, top, inner_w, header_h}, tokens);
    }
    if (content_ != nullptr) {
        content_->Layout(content_rect, tokens);
    }
    if (footer_ != nullptr) {
        footer_->Layout(
            Layout::Rect{inner_x, footer_y, inner_w, footer_h}, tokens);
    }
    // Legacy behavior: unslotted children all fill the content area.
    for (const auto& child : GetChildren()) {
        if (child == header_ || child == content_ || child == footer_) {
            continue;
        }
        child->Layout(content_rect, tokens);
    }

    // Collapse affordance sits at the header strip's trailing edge.
    chevron_rect_ = Layout::Rect{
        bounds.x + bounds.width - pad - kChevronSize,
        bounds.y + media_h + pad +
            std::max(0.0f, (std::max(header_h, kChevronSize) -
                            kChevronSize) * 0.5f),
        kChevronSize, kChevronSize};
}

bool Card::HandleEvent(const Event& event) {
    if (!IsEnabled()) {
        if (held_) {
            held_ = false;
            press_.SetTarget(0.0f);
        }
        return false;
    }
    switch (event.type) {
        case Event::Type::MouseDown:
            if (event.mouse_button == MouseButton::Left &&
                (clickable_ || collapsible_)) {
                held_ = true;
                press_.SetTarget(clickable_ ? 1.0f : 0.4f);
                Invalidate();
                return true;
            }
            return false;
        case Event::Type::MouseUp:
            if (held_ && event.mouse_button == MouseButton::Left) {
                held_ = false;
                press_.SetTarget(0.0f);
                Invalidate();
                return true;
            }
            return false;
        case Event::Type::Click:
            if (event.mouse_button == MouseButton::Left) {
                held_ = false;
                press_.SetTarget(0.0f);
                if (collapsible_ &&
                    chevron_rect_.Contains(event.position)) {
                    SetCollapsed(!collapsed_);
                    return true;
                }
                if (clickable_ && on_click_) {
                    on_click_();
                    return true;
                }
                if (collapsible_) {
                    // Whole-header collapse toggle (views section
                    // headers toggle anywhere on the strip).
                    const auto b = GetBounds();
                    const float pad = PaddingFor(CachedLayoutTokens());
                    const float media_h =
                        media_image_.empty() ? 0.0f : media_height_;
                    const Layout::Rect header_strip{
                        b.x + pad, b.y + media_h + pad,
                        std::max(0.0f, b.width - 2.0f * pad),
                        std::max(HeaderHeight(), kChevronSize)};
                    if (header_strip.Contains(event.position)) {
                        SetCollapsed(!collapsed_);
                        return true;
                    }
                }
            }
            return false;
        case Event::Type::KeyDown:
            if (IsFocused() &&
                (event.key == KeyCode::Enter ||
                 event.key == KeyCode::Space)) {
                if (clickable_ && on_click_) {
                    on_click_();
                    return true;
                }
                if (collapsible_) {
                    SetCollapsed(!collapsed_);
                    return true;
                }
            }
            return false;
        default:
            return false;
    }
}

void Card::Tick(double dt_seconds) {
    if (!IsEffectivelyVisible()) {
        return;
    }
    const float dt = static_cast<float>(dt_seconds);
    const float want_hover =
        IsHovered() && IsEnabled() && clickable_ ? 1.0f : 0.0f;
    if (hover_t_.to != want_hover) {
        hover_t_.Set(want_hover, 0.12f);
    }
    hover_t_.Update(dt);
    press_.Update(dt);
    collapse_t_.Update(dt);
    if (hover_t_.Active() || !press_.Settled()) {
        Invalidate();
    }
    if (collapse_t_.Active()) {
        // The animated content height feeds back into preferred size —
        // the parent must re-flow each frame.
        Invalidate();
        RequestLayout();
    }
}

Layout::Size Card::GetMinSize() const {
    const float pad = density_ == Density::Compact ? kPaddingCompact
                                                   : kPaddingComfortable;
    Layout::Size child_size;
    for (const auto& child : GetChildren()) {
        if (child == header_ || child == footer_) {
            continue;
        }
        const auto size = child->GetMinSize();
        child_size.width = std::max(child_size.width, size.width);
        child_size.height = std::max(child_size.height, size.height);
    }
    const float progress = collapse_t_.Value();
    const float media_h = media_image_.empty() ? 0.0f : media_height_;
    const float regions =
        HeaderHeight() + FooterHeight() + media_h + 2.0f * pad +
        (HeaderHeight() > 0.0f ? kRegionGap : 0.0f) +
        (FooterHeight() > 0.0f ? kRegionGap : 0.0f);
    return {
        std::max(kMinimumDimension, child_size.width + 2.0f * pad),
        std::max(kMinimumDimension, regions + child_size.height * progress)
    };
}

Layout::Size Card::GetPreferredSize() const {
    const float pad = density_ == Density::Compact ? kPaddingCompact
                                                   : kPaddingComfortable;
    Layout::Size child_size;
    for (const auto& child : GetChildren()) {
        if (child == header_ || child == footer_) {
            continue;
        }
        const auto size = child->GetPreferredSize();
        child_size.width = std::max(child_size.width, size.width);
        child_size.height = std::max(child_size.height, size.height);
    }
    float regions_w = 0.0f;
    if (header_ != nullptr) {
        regions_w = std::max(regions_w,
                             header_->GetPreferredSize().width);
    }
    if (footer_ != nullptr) {
        regions_w = std::max(regions_w,
                             footer_->GetPreferredSize().width);
    }
    const float progress = collapse_t_.Value();
    const float media_h = media_image_.empty() ? 0.0f : media_height_;
    const float header_h = HeaderHeight();
    const float footer_h = FooterHeight();
    const float regions =
        header_h + footer_h + media_h + 2.0f * pad +
        (header_h > 0.0f ? kRegionGap : 0.0f) +
        (footer_h > 0.0f ? kRegionGap : 0.0f);
    return {
        std::max(kMinimumDimension,
                 std::max(child_size.width, regions_w) + 2.0f * pad +
                     (collapsible_ ? kChevronSize + kRegionGap : 0.0f)),
        std::max(kMinimumDimension,
                 regions + child_size.height * progress)
    };
}

Layout::Size Card::GetMaxSize() const {
    return {100000.0f, 100000.0f};
}

} // namespace XER::UI::Widgets
