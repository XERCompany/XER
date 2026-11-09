#include "context_menu.h"

#include <algorithm>
#include <utility>

#include "motion/motion.h"
#include "rendering/renderer.h"
#include "theme/tokens.h"
#include "widget_tree.h"

namespace XER::UI::Widgets {
namespace {

constexpr float kItemHeight = 34.0f;
constexpr float kMenuWidth = 216.0f;
constexpr float kSeparatorHeight = 9.0f;
constexpr float kCheckWidth = 22.0f;
constexpr float kMenuPad = 4.0f;
constexpr float kMaxMenuHeight = 460.0f;
constexpr float kScrollbarWidth = 8.0f;

} // namespace

std::shared_ptr<ContextMenu> ContextMenu::Create(std::vector<Item> items) {
    return std::shared_ptr<ContextMenu>(new ContextMenu(std::move(items)));
}

ContextMenu::ContextMenu(std::vector<Item> items)
    : items_(std::move(items)) {
    // Popover entrance: quick ease-out fade + rise (exits stay instant).
    open_t_.Set(1.0f, 0.13f);
}

void ContextMenu::SetAnchor(Layout::Point position) {
    anchor_ = position;
}

void ContextMenu::Close() {
    if (closed_) {
        return;
    }
    closed_ = true;
    // Same single-overlay-slot guard as Dialog::Close: only hide the
    // tree's overlay when it is still this menu.
    const auto self = weak_from_this().lock();
    if (self != nullptr && GetTree() != nullptr &&
        GetTree()->GetOverlay() == self) {
        HideOverlay();
    }
}

Layout::Rect ContextMenu::MenuRect(const Layout::Rect& bounds) const {
    float content_h = kMenuPad * 2.0f;
    for (const auto& item : items_) {
        content_h += item.separator_before ? kSeparatorHeight : 0.0f;
        content_h += kItemHeight;
    }
    content_height_ = content_h;
    // The menu never grows past the viewport (or a sane cap); overflow
    // scrolls instead of spilling off-screen.
    const float avail = bounds.height - 8.0f;
    const float height = std::min({content_h, avail, kMaxMenuHeight});
    viewport_height_ = std::max(0.0f, height - kMenuPad * 2.0f);
    const float w = std::min(kMenuWidth, bounds.width - 8.0f);
    float x = anchor_.x;
    float y = anchor_.y;
    if (x + w > bounds.x + bounds.width - 4.0f) {
        x = bounds.x + bounds.width - w - 4.0f;
    }
    if (y + height > bounds.y + bounds.height - 4.0f) {
        y = bounds.y + bounds.height - height - 4.0f;
    }
    x = std::max(bounds.x + 4.0f, x);
    y = std::max(bounds.y + 4.0f, y);
    return Layout::Rect{x, y, w, height};
}

float ContextMenu::ScrollRange() const {
    return std::max(0.0f, content_height_ - kMenuPad * 2.0f - viewport_height_);
}

void ContextMenu::ScrollTo(float y) {
    const float clamped = std::clamp(y, 0.0f, ScrollRange());
    if (clamped == scroll_y_) {
        return;
    }
    scroll_y_ = clamped;
    Invalidate();
}

void ContextMenu::ScrollFromPointer(float y, bool grab_offset, float grab) {
    const auto menu = MenuRect(GetBounds());
    if (viewport_height_ <= 0.0f || content_height_ <= 0.0f) {
        return;
    }
    const float track_top = menu.y + kMenuPad;
    const float track_h = viewport_height_;
    float thumb_h = track_h * (viewport_height_ / content_height_);
    thumb_h = std::clamp(thumb_h, 24.0f, track_h);
    const float travel = track_h - thumb_h;
    if (travel <= 0.0f) {
        return;
    }
    const float thumb_top = grab_offset ? y - grab : y - thumb_h * 0.5f;
    const float t = std::clamp((thumb_top - track_top) / travel, 0.0f, 1.0f);
    ScrollTo(t * ScrollRange());
}

Layout::Rect ContextMenu::ScrollbarThumbRect() const {
    const float range = ScrollRange();
    const auto menu = MenuRect(GetBounds());
    if (range <= 0.0f || viewport_height_ <= 0.0f || content_height_ <= 0.0f) {
        return {};
    }
    const float track_top = menu.y + kMenuPad;
    const float track_h = viewport_height_;
    float thumb_h = track_h * (viewport_height_ / content_height_);
    thumb_h = std::clamp(thumb_h, 24.0f, track_h);
    const float travel = track_h - thumb_h;
    const float t = scroll_y_ / range;
    return Layout::Rect{menu.x + menu.width - kScrollbarWidth - 2.0f,
                        track_top + t * travel, 4.0f, thumb_h};
}

bool ContextMenu::ScrollbarZoneContains(const Layout::Point& position) const {
    const auto menu = MenuRect(GetBounds());
    return ScrollRange() > 0.0f &&
           position.x >= menu.x + menu.width - kScrollbarWidth - 4.0f &&
           position.x < menu.x + menu.width &&
           position.y >= menu.y + kMenuPad &&
           position.y < menu.y + menu.height - kMenuPad;
}

void ContextMenu::EnsureItemVisible(int index) {
    if (index < 0 || index >= static_cast<int>(item_rects_.size())) {
        return;
    }
    const auto& r = item_rects_[index];
    const float top = r.y - MenuRect(GetBounds()).y - kMenuPad;
    const float bottom = top + r.height;
    if (top < scroll_y_) {
        ScrollTo(top);
    } else if (bottom > scroll_y_ + viewport_height_) {
        ScrollTo(bottom - viewport_height_);
    }
}

void ContextMenu::Tick(double dt_seconds) {
    if (!IsEffectivelyVisible()) {
        return;
    }
    open_t_.Update(static_cast<float>(dt_seconds));
    if (open_t_.Active()) {
        Invalidate();
    }
}

void ContextMenu::Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) {
    const auto bounds = GetBounds();
    const float p = open_t_.Value();
    const auto FA = [p](Theme::Color col) {
        col.a *= p;
        return col;
    };
    Layout::Rect menu = MenuRect(bounds);
    menu.y += (1.0f - p) * -6.0f;
    // Frosted popover: blur the backdrop behind the menu, then dim slightly.
    renderer.DrawBackdropBlur(
        Layout::Rect{menu.x - 8.0f, menu.y - 8.0f, menu.width + 16.0f,
                     menu.height + 16.0f},
        0.7f);
    renderer.FillRect(menu, FA(Theme::Color{0.0f, 0.0f, 0.0f, 0.18f}),
                      tokens.radius_medium);
    // Translucent popover (Tailwind bg-surface/95 glass feel).
    Theme::Color menu_bg = tokens.surface;
    menu_bg.a *= 0.96f;
    renderer.FillRect(menu, FA(menu_bg), tokens.radius_medium);
    renderer.StrokeRect(menu, FA(tokens.border), 1.0f, tokens.radius_medium);

    // Rows are clipped to the inner band so scrolled-off items never spill
    // past the popover edge.
    renderer.PushClip(Layout::Rect{menu.x + 1.0f, menu.y + kMenuPad,
                                   menu.width - 2.0f,
                                   menu.height - kMenuPad * 2.0f});
    float y = menu.y + kMenuPad - scroll_y_;
    const float row_w = menu.width - kMenuPad * 2.0f -
                        (ScrollRange() > 0.0f ? kScrollbarWidth : 0.0f);
    for (std::size_t i = 0; i < items_.size(); ++i) {
        const auto& item = items_[i];
        if (item.separator_before) {
            renderer.DrawLine(
                Layout::Point{menu.x + 6.0f, y + kSeparatorHeight * 0.5f},
                Layout::Point{menu.x + menu.width - 6.0f -
                                  (ScrollRange() > 0.0f ? kScrollbarWidth : 0.0f),
                              y + kSeparatorHeight * 0.5f},
                FA(tokens.border),
                1.0f
            );
            y += kSeparatorHeight;
        }
        const Layout::Rect row{menu.x + kMenuPad, y, row_w, kItemHeight};
        const bool hovered = static_cast<int>(i) == hovered_item_ && item.enabled && IsEnabled();
        if (hovered) {
            renderer.FillRect(row, FA(item.danger ? Theme::Color{tokens.danger.r,
                                                                  tokens.danger.g,
                                                                  tokens.danger.b,
                                                                  0.14f}
                                                  : tokens.hover),
                             tokens.radius_small);
        }
        Theme::Color label_color = item.danger ? tokens.danger : tokens.foreground;
        if (!item.enabled) {
            label_color = tokens.muted;
        }
        const Rendering::TextStyle style{
            FA(label_color),
            tokens.font_body,
            400
        };
        const Rendering::TextStyle check_style{
            FA(tokens.accent),
            tokens.font_body,
            600
        };
        const float text_h = renderer.MeasureText(item.label, style).height;
        const float text_y = row.y + (kItemHeight - text_h) * 0.5f;
        float text_x = row.x + tokens.spacing_medium;
        if (item.checked) {
            renderer.DrawText("✓", Layout::Rect{row.x + 8.0f, text_y, kCheckWidth, text_h},
                              check_style);
            text_x += kCheckWidth;
        }
        renderer.DrawText(
            item.label,
            Layout::Rect{text_x, text_y,
                         row.x + row.width - text_x - tokens.spacing_medium -
                             (item.shortcut.empty() ? 0.0f : 90.0f),
                         text_h},
            style
        );
        if (!item.shortcut.empty()) {
            const Rendering::TextStyle hint_style{FA(tokens.muted), tokens.font_small, 400};
            const auto hint_size = renderer.MeasureText(item.shortcut, hint_style);
            renderer.DrawText(
                item.shortcut,
                Layout::Rect{row.x + row.width - hint_size.width - tokens.spacing_medium,
                             text_y, hint_size.width, text_h},
                hint_style
            );
        }
        y += kItemHeight;
    }
    renderer.PopClip();

    // Scrollbar thumb (only when the list overflows).
    const auto thumb = ScrollbarThumbRect();
    if (thumb.width > 0.0f && thumb.height > 0.0f) {
        renderer.FillRect(
            thumb,
            FA(scrollbar_dragging_
                   ? Theme::Color{tokens.muted.r, tokens.muted.g,
                                  tokens.muted.b, 0.85f}
                   : Theme::Color{tokens.muted.r, tokens.muted.g,
                                  tokens.muted.b, 0.45f}),
            2.0f);
    }
}

void ContextMenu::Layout(const Layout::Rect& bounds) {
    SetBounds(bounds);
    item_rects_.clear();
    const auto menu = MenuRect(bounds);
    float y = menu.y + kMenuPad;
    for (const auto& item : items_) {
        if (item.separator_before) {
            y += kSeparatorHeight;
        }
        item_rects_.push_back(
            Layout::Rect{menu.x + kMenuPad, y, menu.width - kMenuPad * 2.0f,
                         kItemHeight});
        y += kItemHeight;
    }
    scroll_y_ = std::clamp(scroll_y_, 0.0f, ScrollRange());
}

bool ContextMenu::HandleEvent(const Event& event) {
    if (closed_) {
        return false;
    }
    const auto menu = MenuRect(GetBounds());
    switch (event.type) {
        case Event::Type::MouseWheel:
            if (ScrollRange() > 0.0f && menu.Contains(event.position)) {
                // One notch ≈ half a viewport, floored at ~2 items.
                const float step = std::max(kItemHeight * 2.0f,
                                            viewport_height_ * 0.5f);
                ScrollTo(scroll_y_ -
                         static_cast<float>(event.wheel_delta) / 120.0f * step);
                return true;
            }
            return false;
        case Event::Type::MouseDown:
            // A stale suppress_click_ (scrollbar drag released outside the
            // menu, so no synthesized click ever consumed it) must not eat
            // the next real click. Every press re-arms it below if needed.
            suppress_click_ = false;
            if (event.mouse_button == MouseButton::Left &&
                ScrollbarZoneContains(event.position)) {
                const auto thumb = ScrollbarThumbRect();
                const float grab = event.position.y - thumb.y;
                scrollbar_grab_ = grab;
                if (grab < 0.0f || grab > thumb.height) {
                    // Track click pages toward the click.
                    const float mid = thumb.y + thumb.height * 0.5f;
                    ScrollTo(scroll_y_ +
                             (event.position.y < mid
                                  ? -viewport_height_ * 0.8f
                                  : viewport_height_ * 0.8f));
                    scrollbar_grab_ =
                        ScrollbarThumbRect().height * 0.5f;
                }
                scrollbar_dragging_ = true;
                suppress_click_ = true;
                return true;
            }
            return false;
        case Event::Type::MouseMove:
            if (scrollbar_dragging_) {
                ScrollFromPointer(event.position.y, true, scrollbar_grab_);
                return true;
            }
            {
                const int hit = HitItem(event.position);
                if (hit != hovered_item_) {
                    hovered_item_ = hit;
                    Invalidate();
                }
                return hit >= 0;
            }
        case Event::Type::MouseUp:
            if (event.mouse_button == MouseButton::Left && scrollbar_dragging_) {
                scrollbar_dragging_ = false;
                suppress_click_ = true;
                return true;
            }
            return false;
        case Event::Type::Click: {
            if (suppress_click_) {
                // The press was a scrollbar interaction; swallow the
                // synthesized click so it can't activate an item.
                suppress_click_ = false;
                return true;
            }
            if (!menu.Contains(event.position)) {
                Close();
                return true;
            }
            const int index = HitItem(event.position);
            if (index >= 0 && index < static_cast<int>(items_.size()) && items_[static_cast<std::size_t>(index)].enabled) {
                auto action = items_[static_cast<std::size_t>(index)].action;
                Close();
                if (action) {
                    action();
                }
            }
            return true;
        }
        case Event::Type::KeyDown:
            switch (event.key) {
                case KeyCode::Escape:
                    Close();
                    return true;
                case KeyCode::ArrowUp:
                case KeyCode::ArrowDown: {
                    const int dir = event.key == KeyCode::ArrowDown ? 1 : -1;
                    const int n = static_cast<int>(items_.size());
                    if (n == 0) return true;
                    int i = hovered_item_;
                    for (int step = 0; step < n; ++step) {
                        i += dir;
                        if (i < 0) i = n - 1;
                        if (i >= n) i = 0;
                        if (items_[static_cast<std::size_t>(i)].enabled) break;
                    }
                    if (items_[static_cast<std::size_t>(i)].enabled &&
                        i != hovered_item_) {
                        hovered_item_ = i;
                        EnsureItemVisible(i);
                        Invalidate();
                    }
                    return true;
                }
                case KeyCode::Enter: {
                    if (hovered_item_ >= 0 &&
                        hovered_item_ < static_cast<int>(items_.size()) &&
                        items_[static_cast<std::size_t>(hovered_item_)].enabled) {
                        auto action = items_[static_cast<std::size_t>(hovered_item_)].action;
                        Close();
                        if (action) {
                            action();
                        }
                    }
                    return true;
                }
                default:
                    return false;
            }
        default:
            return false;
    }
}

int ContextMenu::HitItem(const Layout::Point& position) const {
    if (ScrollbarZoneContains(position)) {
        return -1;
    }
    // item_rects_ live in unscrolled content space; the pointer maps into
    // that space by adding the current scroll offset.
    const Layout::Point doc{position.x, position.y + scroll_y_};
    for (std::size_t i = 0; i < item_rects_.size(); ++i) {
        if (item_rects_[i].Contains(doc)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

Layout::Size ContextMenu::GetMinSize() const {
    return {0.0f, 0.0f};
}

Layout::Size ContextMenu::GetPreferredSize() const {
    return {kMenuWidth, kItemHeight * static_cast<float>(items_.size())};
}

Layout::Size ContextMenu::GetMaxSize() const {
    return {100000.0f, 100000.0f};
}

} // namespace XER::UI::Widgets
