#include "virtual_list.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "rendering/renderer.h"
#include "theme/tokens.h"

namespace XER::UI::Widgets {
namespace {

// Floating-scrollbar metrics mirror ScrollArea's (scroll_area.h) so the
// two chrome styles stay visually identical across the kit.
constexpr float kBarWidth = 8.0f;
constexpr float kBarPad = 2.0f;
constexpr float kHitWidth = 14.0f;
constexpr float kMinThumb = 24.0f;

Theme::Color WithAlpha(Theme::Color color, float alpha) {
    color.a *= alpha;
    return color;
}

} // namespace

std::shared_ptr<VirtualList> VirtualList::Create() {
    return std::shared_ptr<VirtualList>(new VirtualList());
}

VirtualList::VirtualList() {
    // Keyboard navigation is the point of the widget — take focus like
    // the other list surfaces (click routing in WidgetTree handles it).
    SetFocusable(true);
}

void VirtualList::SetItemCount(int count) {
    item_count_ = std::max(0, count);
    for (auto it = selected_.begin(); it != selected_.end();) {
        if (*it >= item_count_) {
            it = selected_.erase(it);
        } else {
            ++it;
        }
    }
    if (cursor_ >= item_count_) cursor_ = item_count_ - 1;
    if (anchor_ >= item_count_) anchor_ = item_count_ - 1;
    if (hovered_ >= item_count_) hovered_ = -1;
    RequestLayout();
    Invalidate();
}

void VirtualList::SetItemHeight(float px) {
    item_height_ = std::max(18.0f, px);
    RequestLayout();
    Invalidate();
}

void VirtualList::SetItemRenderer(ItemRenderer cb) {
    item_renderer_ = std::move(cb);
    Invalidate();
}

void VirtualList::SetOverscan(int rows) {
    overscan_ = std::clamp(rows, 0, 64);
    Invalidate();
}

void VirtualList::SetSelectionMode(SelectionMode mode) {
    if (selection_mode_ == mode) return;
    selection_mode_ = mode;
    if (mode == SelectionMode::None) {
        selected_.clear();
    } else if (mode == SelectionMode::Single && selected_.size() > 1) {
        const int keep = *selected_.begin();
        selected_.clear();
        selected_.insert(keep);
    }
    Invalidate();
}

void VirtualList::SetSelectedIndex(int index) {
    selected_.clear();
    if (index >= 0 && index < item_count_ &&
        selection_mode_ != SelectionMode::None) {
        selected_.insert(index);
    }
    Invalidate();
}

int VirtualList::GetSelectedIndex() const {
    return selected_.empty() ? -1 : *selected_.begin();
}

void VirtualList::SetSelectedIndices(std::set<int> indices) {
    for (auto it = indices.begin(); it != indices.end();) {
        if (*it < 0 || *it >= item_count_) {
            it = indices.erase(it);
        } else {
            ++it;
        }
    }
    if (selection_mode_ == SelectionMode::None) {
        indices.clear();
    } else if (selection_mode_ == SelectionMode::Single &&
               indices.size() > 1) {
        const int keep = *indices.begin();
        indices.clear();
        indices.insert(keep);
    }
    selected_ = std::move(indices);
    if (!selected_.empty()) anchor_ = *selected_.begin();
    Invalidate();
}

void VirtualList::SetOnSelectionChanged(std::function<void()> cb) {
    on_selection_changed_ = std::move(cb);
}

void VirtualList::SetOnItemActivated(std::function<void(int)> cb) {
    on_item_activated_ = std::move(cb);
}

void VirtualList::SetOnItemClick(
    std::function<bool(int, Layout::Point)> cb) {
    on_item_click_ = std::move(cb);
}

void VirtualList::SetKeyFallback(std::function<bool(const Event&)> cb) {
    key_fallback_ = std::move(cb);
}

void VirtualList::SetCursorIndex(int index) {
    const int clamped = (item_count_ == 0)
        ? -1
        : std::clamp(index, 0, item_count_ - 1);
    if (clamped == cursor_) return;
    cursor_ = clamped;
    if (cursor_ >= 0) EnsureVisible(cursor_);
    Invalidate();
}

void VirtualList::ScrollTo(float y) {
    const float clamped = std::clamp(y, 0.0f, max_scroll_);
    if (clamped == scroll_) return;
    scroll_ = clamped;
    Invalidate();
}

void VirtualList::ScrollToIndex(int index) {
    if (index < 0 || index >= item_count_) return;
    const auto b = GetBounds();
    const float y = item_height_ * static_cast<float>(index);
    ScrollTo(y - (b.height - item_height_) * 0.5f);
}

void VirtualList::EnsureVisible(int index) {
    if (index < 0 || index >= item_count_) return;
    const auto b = GetBounds();
    const float top = item_height_ * static_cast<float>(index);
    const float bottom = top + item_height_;
    if (top < scroll_) {
        ScrollTo(top);
    } else if (bottom > scroll_ + b.height) {
        ScrollTo(bottom - b.height);
    }
}

int VirtualList::FirstPaintedIndex() const {
    if (item_count_ == 0) return -1;
    return std::max(0, static_cast<int>(std::floor(scroll_ / item_height_)) -
                       overscan_);
}

int VirtualList::LastPaintedIndex() const {
    if (item_count_ == 0) return -1;
    const float viewport = GetBounds().height;
    const int last_visible = static_cast<int>(
        std::floor((scroll_ + viewport) / item_height_));
    return std::min(item_count_ - 1, last_visible + overscan_);
}

int VirtualList::PaintedRangeEnd() const {
    return LastPaintedIndex();
}

int VirtualList::RowAtY(float y) const {
    const auto b = GetBounds();
    const int idx = static_cast<int>(
        std::floor((y - b.y + scroll_) / item_height_));
    return (idx >= 0 && idx < item_count_) ? idx : -1;
}

void VirtualList::Layout(const Layout::Rect& bounds) {
    SetBounds(bounds);
    const auto b = GetBounds();
    const float content_h = GetContentHeight();
    max_scroll_ = std::max(0.0f, content_h - b.height);
    scroll_ = std::clamp(scroll_, 0.0f, max_scroll_);
    scrollbar_rect_ = {b.x + b.width - kHitWidth, b.y,
                       kHitWidth, b.height};
}

void VirtualList::Render(Rendering::Renderer& renderer,
                         const Theme::Tokens& tokens) {
    const auto b = GetBounds();
    if (b.width <= 0.0f || b.height <= 0.0f) return;

    renderer.PushClip(b);
    const int first = FirstPaintedIndex();
    const int last = PaintedRangeEnd();
    for (int i = first; i >= 0 && i <= last; ++i) {
        const float ry = b.y + item_height_ * static_cast<float>(i) - scroll_;
        const Layout::Rect row{b.x, ry, b.width, item_height_};
        const bool sel = IsRowSelected(i);
        const bool hov = i == hovered_;
        if (sel) {
            // Table's editor-selection idiom: accent wash + leading edge.
            renderer.FillRect(row, tokens.selection, 0.0f);
            renderer.FillRect({row.x, row.y, 3.0f, row.height},
                              tokens.accent, 0.0f);
        } else if (hov) {
            renderer.FillRect(row, tokens.hover, 0.0f);
        }
        if (i == cursor_ && IsFocused()) {
            // Keyboard-cursor affordance: a 1px inner ring, kept inside
            // the row so embedded lists (TreeView) don't clip it.
            renderer.StrokeRect(
                {row.x + 1.0f, row.y + 1.0f,
                 std::max(0.0f, row.width - 2.0f),
                 std::max(0.0f, row.height - 2.0f)},
                WithAlpha(tokens.ring, 0.8f), 1.0f,
                tokens.radius_small);
        }
        if (item_renderer_) {
            item_renderer_(renderer, row, i, sel);
        }
    }
    renderer.PopClip();

    // Scrollbar thumb over the clipped rows (ScrollArea visuals).
    if (max_scroll_ > 0.0f) {
        const auto thumb = ScrollbarThumbRect();
        if (scrollbar_hovered_ || scrollbar_drag_) {
            const Layout::Rect track{b.x + b.width - kBarPad - kBarWidth,
                                     b.y + kBarPad, kBarWidth,
                                     std::max(0.0f, b.height - 2.0f * kBarPad)};
            renderer.FillRect(track, WithAlpha(tokens.border, 0.45f),
                              track.width * 0.5f);
        }
        const bool active = scrollbar_drag_ || scrollbar_hovered_;
        const Theme::Color thumb_color =
            scrollbar_drag_ ? tokens.foreground
                            : (active ? tokens.foreground : tokens.muted);
        const float alpha =
            scrollbar_drag_ ? 0.95f : (scrollbar_hovered_ ? 0.85f : 0.5f);
        if (thumb.width > 0.0f && thumb.height > 0.0f) {
            renderer.FillRect(thumb, WithAlpha(thumb_color, alpha),
                              thumb.width * 0.5f);
        }
    }

    // Focus ring: drawn inside the bounds (not RenderFocusRing's outside
    // offset) so it survives tight embeddings — TreeView insets this list
    // 1px inside its own border and a ring drawn outside would clip.
    if (IsFocused()) {
        renderer.StrokeRect(
            {b.x + 1.0f, b.y + 1.0f,
             std::max(0.0f, b.width - 2.0f),
             std::max(0.0f, b.height - 2.0f)},
            tokens.ring, 2.0f, tokens.radius_small);
    }
}

bool VirtualList::ScrollbarZoneContains(const Layout::Point& p) const {
    return max_scroll_ > 0.0f &&
           p.x >= scrollbar_rect_.x &&
           p.x < scrollbar_rect_.x + scrollbar_rect_.width &&
           p.y >= scrollbar_rect_.y &&
           p.y < scrollbar_rect_.y + scrollbar_rect_.height;
}

Layout::Rect VirtualList::ScrollbarThumbRect() const {
    const auto b = GetBounds();
    const float track_h = std::max(0.0f, b.height - 2.0f * kBarPad);
    const float content_h = GetContentHeight();
    if (max_scroll_ <= 0.0f || track_h <= 0.0f || content_h <= 0.0f) {
        return {};
    }
    const float thumb_h =
        std::clamp(track_h * (b.height / content_h), kMinThumb, track_h);
    const float travel = track_h - thumb_h;
    const float t = scroll_ / max_scroll_;
    return {b.x + b.width - kBarPad - kBarWidth,
            b.y + kBarPad + t * travel, kBarWidth, thumb_h};
}

void VirtualList::ScrollFromPointer(float y) {
    const auto b = GetBounds();
    const float track_h = std::max(0.0f, b.height - 2.0f * kBarPad);
    const float content_h = GetContentHeight();
    if (track_h <= 0.0f || content_h <= 0.0f) return;
    const float thumb_h =
        std::clamp(track_h * (b.height / content_h), kMinThumb, track_h);
    const float travel = track_h - thumb_h;
    if (travel <= 0.0f) return;
    const float thumb_top = y - scrollbar_grab_;
    const float t = std::clamp((thumb_top - (b.y + kBarPad)) / travel,
                               0.0f, 1.0f);
    ScrollTo(t * max_scroll_);
}

void VirtualList::ApplyCursorSelection() {
    // Shared tail for cursor moves that also own selection (Single mode,
    // or shift-extension in Multiple mode).
    if (cursor_ < 0) return;
    bool changed = false;
    if (selection_mode_ == SelectionMode::Single) {
        if (!(selected_.size() == 1 && *selected_.begin() == cursor_)) {
            selected_.clear();
            selected_.insert(cursor_);
            anchor_ = cursor_;
            changed = true;
        }
    } else if (selection_mode_ == SelectionMode::Multiple &&
               anchor_ >= 0) {
        std::set<int> range;
        const int lo = std::min(anchor_, cursor_);
        const int hi = std::max(anchor_, cursor_);
        for (int i = lo; i <= hi; ++i) range.insert(i);
        if (range != selected_) {
            selected_ = std::move(range);
            changed = true;
        }
    }
    EnsureVisible(cursor_);
    Invalidate();
    if (changed && on_selection_changed_) on_selection_changed_();
}

void VirtualList::MoveCursor(int delta, bool extend_selection) {
    if (item_count_ == 0) return;
    const int base = cursor_ >= 0 ? cursor_
                                  : (delta > 0 ? -1 : item_count_);
    cursor_ = std::clamp(base + delta, 0, item_count_ - 1);
    if (!extend_selection) anchor_ = cursor_;
    const bool own_selection =
        selection_mode_ == SelectionMode::Single ||
        (selection_mode_ == SelectionMode::Multiple && extend_selection);
    if (own_selection) {
        ApplyCursorSelection();
    } else {
        EnsureVisible(cursor_);
        Invalidate();
    }
}

void VirtualList::SetCursorPage(int delta_pages, bool extend_selection) {
    const auto b = GetBounds();
    const int page = std::max(
        1, static_cast<int>(b.height / item_height_) - 1);
    MoveCursor(delta_pages * page, extend_selection);
}

bool VirtualList::HandleEvent(const Event& event) {
    if (!IsEnabled()) {
        return false;
    }
    const auto& p = event.position;

    switch (event.type) {
        case Event::Type::MouseMove: {
            if (scrollbar_drag_) {
                ScrollFromPointer(p.y);
                return true;
            }
            const int row = RowAtY(p.y);
            const bool bar = ScrollbarZoneContains(p);
            if (row != hovered_ || bar != scrollbar_hovered_) {
                hovered_ = row;
                scrollbar_hovered_ = bar;
                Invalidate();
            }
            return false;
        }
        case Event::Type::MouseDown: {
            if (event.mouse_button == MouseButton::Left &&
                ScrollbarZoneContains(p)) {
                const auto thumb = ScrollbarThumbRect();
                scrollbar_grab_ = p.y - thumb.y;
                if (scrollbar_grab_ < 0.0f ||
                    scrollbar_grab_ > thumb.height) {
                    // Track click pages toward the pointer, then the drag
                    // continues from the thumb center (ScrollArea idiom).
                    const float mid = thumb.y + thumb.height * 0.5f;
                    const auto b = GetBounds();
                    ScrollTo(scroll_ + (p.y < mid ? -b.height * 0.8f
                                                  : b.height * 0.8f));
                    scrollbar_grab_ = ScrollbarThumbRect().height * 0.5f;
                }
                scrollbar_drag_ = true;
                return true;
            }
            return false;
        }
        case Event::Type::MouseUp:
            if (scrollbar_drag_ && event.mouse_button == MouseButton::Left) {
                scrollbar_drag_ = false;
                return true;
            }
            return false;
        case Event::Type::Click: {
            const int row = RowAtY(p.y);
            if (row < 0) return false;
            // Sub-row zones (tree chevrons, checkboxes) get first refusal.
            if (on_item_click_ && on_item_click_(row, p)) {
                return true;
            }
            cursor_ = row;
            bool changed = false;
            switch (selection_mode_) {
                case SelectionMode::None:
                    break;
                case SelectionMode::Single:
                    if (!(selected_.size() == 1 &&
                          *selected_.begin() == row)) {
                        selected_.clear();
                        selected_.insert(row);
                        changed = true;
                    }
                    anchor_ = row;
                    break;
                case SelectionMode::Multiple:
                    if (event.is_shift && anchor_ >= 0) {
                        std::set<int> range;
                        const int lo = std::min(anchor_, row);
                        const int hi = std::max(anchor_, row);
                        for (int i = lo; i <= hi; ++i) range.insert(i);
                        if (range != selected_) {
                            selected_ = std::move(range);
                            changed = true;
                        }
                    } else if (event.is_ctrl) {
                        if (selected_.count(row)) {
                            selected_.erase(row);
                        } else {
                            selected_.insert(row);
                        }
                        anchor_ = row;
                        changed = true;
                    } else {
                        if (!(selected_.size() == 1 &&
                              *selected_.begin() == row)) {
                            selected_.clear();
                            selected_.insert(row);
                            changed = true;
                        }
                        anchor_ = row;
                    }
                    break;
            }
            Invalidate();
            if (changed && on_selection_changed_) on_selection_changed_();
            return true;
        }
        case Event::Type::MouseWheel:
            // Same convention as Table/ScrollArea: three rows per notch.
            ScrollTo(scroll_ - static_cast<float>(event.wheel_delta) /
                                   120.0f * item_height_ * 3.0f);
            return true;
        case Event::Type::KeyDown: {
            const bool extend = event.is_shift;
            switch (event.key) {
                case KeyCode::ArrowDown:
                    MoveCursor(1, extend);
                    return true;
                case KeyCode::ArrowUp:
                    MoveCursor(-1, extend);
                    return true;
                case KeyCode::PageDown:
                    SetCursorPage(1, extend);
                    return true;
                case KeyCode::PageUp:
                    SetCursorPage(-1, extend);
                    return true;
                case KeyCode::Home:
                    SetCursorIndex(0);
                    if (extend ||
                        selection_mode_ == SelectionMode::Single) {
                        ApplyCursorSelection();
                    }
                    return true;
                case KeyCode::End:
                    SetCursorIndex(item_count_ - 1);
                    if (extend ||
                        selection_mode_ == SelectionMode::Single) {
                        ApplyCursorSelection();
                    }
                    return true;
                case KeyCode::Space:
                    if (selection_mode_ == SelectionMode::Multiple &&
                        cursor_ >= 0) {
                        if (selected_.count(cursor_)) {
                            selected_.erase(cursor_);
                        } else {
                            selected_.insert(cursor_);
                        }
                        anchor_ = cursor_;
                        Invalidate();
                        if (on_selection_changed_) on_selection_changed_();
                        return true;
                    }
                    if (cursor_ >= 0 && on_item_activated_) {
                        on_item_activated_(cursor_);
                        return true;
                    }
                    break;
                case KeyCode::Enter:
                    if (cursor_ >= 0 && on_item_activated_) {
                        on_item_activated_(cursor_);
                        return true;
                    }
                    break;
                default:
                    break;
            }
            if (key_fallback_) return key_fallback_(event);
            return false;
        }
        case Event::Type::TextInput:
            // The kit reports letter keys only as Ctrl+KeyCode (see
            // widget.h) — typeahead-style features must consume
            // TextInput. The list itself has none, so everything goes to
            // the fallback hook (TreeView's typeahead lives there).
            if (key_fallback_) return key_fallback_(event);
            return false;
        default:
            return false;
    }
}

Layout::Size VirtualList::GetMinSize() const {
    return {80.0f, item_height_};
}

Layout::Size VirtualList::GetPreferredSize() const {
    return {240.0f, item_height_ * 8.0f};
}

Layout::Size VirtualList::GetMaxSize() const {
    return {100000.0f, 100000.0f};
}

} // namespace XER::UI::Widgets
