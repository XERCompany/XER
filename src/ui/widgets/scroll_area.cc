#include "scroll_area.h"

#include <algorithm>
#include <cmath>

#include "rendering/renderer.h"
#include "theme/tokens.h"

namespace XER::UI::Widgets {
namespace {

Theme::Color WithAlpha(Theme::Color color, float alpha) {
    color.a *= alpha;
    return color;
}

} // namespace

// ---------------------------------------------------------------------------
// ScrollBarWidget
// ---------------------------------------------------------------------------

ScrollBarWidget::ScrollBarWidget(ScrollArea* owner)
    : owner_(owner) {}

void ScrollBarWidget::Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) {
    if (owner_ == nullptr || owner_->GetScrollRange() <= 0.0f) {
        return;
    }
    const auto thumb = owner_->ScrollbarThumbRect();
    if (thumb.width <= 0.0f || thumb.height <= 0.0f) {
        return;
    }
    // Track: barely-there rounded rail.
    renderer.FillRect(GetBounds(), WithAlpha(tokens.border, 0.35f), tokens.radius_small);
    // Thumb: muted at rest, stronger on hover (hover state is managed by
    // WidgetTree::UpdateHover).
    const auto thumb_color = IsHovered() ? tokens.foreground : tokens.muted;
    renderer.FillRect(thumb, WithAlpha(thumb_color, IsHovered() ? 0.85f : 0.55f),
                      tokens.radius_small);
}

void ScrollBarWidget::Layout(const Layout::Rect& bounds) {
    SetBounds(bounds);
}

bool ScrollBarWidget::HandleEvent(const Event& event) {
    if (owner_ == nullptr) {
        return false;
    }
    switch (event.type) {
    case Event::Type::MouseMove:
        if (owner_->scrollbar_dragging_) {
            if (!owner_->IsEnabled()) {
                // Disabled mid-drag: settle the drag instead of leaving
                // scrollbar_dragging_ stuck on.
                owner_->scrollbar_dragging_ = false;
                return true;
            }
            owner_->ScrollFromPointer(event.position.y, true, owner_->scrollbar_grab_);
            return true;
        }
        return false;
    case Event::Type::MouseDown:
        if (!owner_->IsEnabled()) {
            return false;
        }
        if (event.mouse_button == MouseButton::Left &&
            owner_->ScrollbarZoneContains(event.position)) {
            const auto thumb = owner_->ScrollbarThumbRect();
            const float grab = event.position.y - thumb.y;
            owner_->scrollbar_grab_ = grab;
            if (grab < 0.0f || grab > thumb.height) {
                // Click on the track pages toward the click.
                const float mid = thumb.y + thumb.height * 0.5f;
                owner_->ScrollTo(owner_->GetScrollY() +
                    (event.position.y < mid ? -owner_->viewport_height_ * 0.8f
                                            : owner_->viewport_height_ * 0.8f));
                owner_->scrollbar_grab_ = owner_->ScrollbarThumbRect().height * 0.5f;
            }
            owner_->scrollbar_dragging_ = true;
            return true;
        }
        return false;
    case Event::Type::MouseUp:
        if (event.mouse_button == MouseButton::Left && owner_->scrollbar_dragging_) {
            owner_->scrollbar_dragging_ = false;
            return true;
        }
        return false;
    default:
        return false;
    }
}

// ---------------------------------------------------------------------------
// ScrollArea
// ---------------------------------------------------------------------------

ScrollArea::ScrollArea(std::shared_ptr<Widget> content)
    : content_(std::move(content))
    , scrollbar_(std::make_shared<ScrollBarWidget>(this)) {
    SetClipsChildren(true);
}

void ScrollArea::EnsureChildrenAttached() {
    if (children_attached_) {
        return;
    }
    // Order matters: the scrollbar must be the LAST child so reverse-order
    // hit-testing gives it priority over the content in its strip.
    AddChild(content_);
    AddChild(scrollbar_);
    children_attached_ = true;
}

void ScrollArea::SetContent(std::shared_ptr<Widget> content) {
    if (content_ != nullptr) {
        RemoveChild(content_);
    }
    RemoveChild(scrollbar_);
    content_ = std::move(content);
    children_attached_ = false;
    scroll_y_ = 0.0f;
    content_height_ = 0.0f;
    RequestLayout();
    Invalidate();
}

void ScrollArea::ScrollTo(float y) {
    const float clamped = std::clamp(y, 0.0f, scroll_range_);
    if (clamped == scroll_y_) {
        return;
    }
    scroll_y_ = clamped;
    RelayoutContent();
    Invalidate();
}

void ScrollArea::Render(Rendering::Renderer&, const Theme::Tokens&) {
    // The viewport background is painted by the window/root widget; the
    // scrollbar child draws itself on top of the clipped content.
}

void ScrollArea::Layout(const Layout::Rect& bounds) {
    Layout(bounds, last_tokens_);
}

void ScrollArea::Layout(const Layout::Rect& bounds, const Theme::Tokens& tokens) {
    CacheLayoutTokens(tokens);
    last_tokens_ = tokens;
    EnsureChildrenAttached();
    SetBounds(bounds);
    viewport_height_ = bounds.height;

    if (content_ == nullptr) {
        UpdateScrollRange();
        scrollbar_->Layout(Layout::Rect{bounds.x + bounds.width - kBarWidth - kBarPad,
                                        bounds.y, kBarWidth, bounds.height}, CachedLayoutTokens());
        return;
    }

    // Estimate with the last known height, lay out, then correct if the
    // content reports a different height (first layout or node changes).
    const float estimate = content_height_ > 0.0f ? content_height_
                                                 : content_->GetContentHeight();
    const float content_h = std::max(estimate, viewport_height_);
    content_->Layout(Layout::Rect{bounds.x, bounds.y - scroll_y_,
                                  bounds.width, content_h}, tokens);
    content_->SetVisibleRect(Layout::Rect{bounds.x, bounds.y,
                                          bounds.width, bounds.height});

    const float actual = content_->GetContentHeight();
    content_height_ = actual > 0.0f ? actual : estimate;
    UpdateScrollRange();
    const float clamped_scroll = std::clamp(scroll_y_, 0.0f, scroll_range_);

    // If the real height differed from the estimate, or the scroll offset
    // was clamped, positions computed with stale values may be off —
    // relayout once with corrected values.
    if (std::fabs(actual - estimate) > 0.5f || clamped_scroll != scroll_y_) {
        scroll_y_ = clamped_scroll;
        content_->Layout(Layout::Rect{bounds.x, bounds.y - scroll_y_,
                                      bounds.width,
                                      std::max(content_height_, viewport_height_)},
                         tokens);
        content_->SetVisibleRect(Layout::Rect{bounds.x, bounds.y,
                                              bounds.width, bounds.height});
    }

    scrollbar_->Layout(Layout::Rect{bounds.x + bounds.width - kBarWidth - kBarPad,
                                    bounds.y, kBarWidth, bounds.height}, CachedLayoutTokens());
}

void ScrollArea::RelayoutContent() {
    const auto bounds = GetBounds();
    if (content_ == nullptr || bounds.width <= 0.0f) {
        return;
    }
    content_->Layout(Layout::Rect{bounds.x, bounds.y - scroll_y_, bounds.width,
                                  std::max(content_height_, bounds.height)},
                     last_tokens_);
    content_->SetVisibleRect(Layout::Rect{bounds.x, bounds.y,
                                          bounds.width, bounds.height});
}

void ScrollArea::UpdateScrollRange() {
    scroll_range_ = std::max(0.0f, content_height_ - viewport_height_);
}

float ScrollArea::ScrollbarTrackTop() const {
    return GetBounds().y + kBarPad;
}

float ScrollArea::ScrollbarTrackHeight() const {
    return std::max(0.0f, GetBounds().height - 2.0f * kBarPad);
}

Layout::Rect ScrollArea::ScrollbarThumbRect() const {
    const float track_h = ScrollbarTrackHeight();
    if (scroll_range_ <= 0.0f || track_h <= 0.0f || content_height_ <= 0.0f) {
        return {};
    }
    float thumb_h = track_h * (viewport_height_ / content_height_);
    thumb_h = std::clamp(thumb_h, 24.0f, track_h);
    const float travel = track_h - thumb_h;
    const float t = scroll_range_ > 0.0f ? scroll_y_ / scroll_range_ : 0.0f;
    const float thumb_y = ScrollbarTrackTop() + t * travel;
    const auto bounds = GetBounds();
    return Layout::Rect{bounds.x + bounds.width - kBarWidth,
                        thumb_y, kBarWidth, thumb_h};
}

void ScrollArea::ScrollFromPointer(float y, bool grab_offset, float grab) {
    const float track_h = ScrollbarTrackHeight();
    if (track_h <= 0.0f) {
        return;
    }
    float thumb_h = track_h * (viewport_height_ / std::max(content_height_, 1.0f));
    thumb_h = std::clamp(thumb_h, 24.0f, track_h);
    const float travel = track_h - thumb_h;
    if (travel <= 0.0f) {
        return;
    }
    const float thumb_top = grab_offset ? y - grab : y - thumb_h * 0.5f;
    const float t = std::clamp((thumb_top - ScrollbarTrackTop()) / travel, 0.0f, 1.0f);
    ScrollTo(t * scroll_range_);
}

bool ScrollArea::ScrollbarZoneContains(const Layout::Point& position) const {
    const auto bounds = GetBounds();
    // Without scroll range the bar is invisible — don't let it swallow input.
    return scroll_range_ > 0.0f &&
           position.x >= bounds.x + bounds.width - kBarWidth - kBarPad &&
           position.x < bounds.x + bounds.width &&
           position.y >= bounds.y && position.y < bounds.y + bounds.height;
}

bool ScrollArea::HandleEvent(const Event& event) {
    if (!IsEnabled()) {
        return false;
    }
    if (event.type == Event::Type::MouseWheel && scroll_range_ > 0.0f) {
        // One notch ≈ 12% of the viewport, clamped to a sane minimum.
        const float step = std::max(48.0f, viewport_height_ * 0.12f);
        ScrollTo(scroll_y_ - static_cast<float>(event.wheel_delta) / 120.0f * step);
        return true;
    }
    return false;
}

} // namespace XER::UI::Widgets
