#include "carousel.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "rendering/renderer.h"
#include "theme/tokens.h"

namespace XER::UI::Widgets {

std::shared_ptr<Carousel> Carousel::Create() {
    return std::shared_ptr<Carousel>(new Carousel());
}

Carousel::Carousel() {
    SetClipsChildren(true);
    scroll_t_.Set(0.0f, 0.28f);
}

void Carousel::AddSlide(std::shared_ptr<Widget> slide) {
    if (!slide) return;
    AddChild(slide);
    slides_.push_back(std::move(slide));
    RequestLayout();
}

void Carousel::ClearSlides() {
    slides_.clear();
    index_ = 0;
    scroll_ = target_scroll_ = 0.0f;
    RequestLayout();
}

void Carousel::SetIndex(int index) { GoTo(index, true); }
void Carousel::Next() { GoTo(index_ + 1, true); }
void Carousel::Prev() { GoTo(index_ - 1, true); }

void Carousel::SetLoop(bool loop) { loop_ = loop; }
void Carousel::SetShowArrows(bool show) { show_arrows_ = show; Invalidate(); }
void Carousel::SetShowDots(bool show) { show_dots_ = show; Invalidate(); }

void Carousel::SetOnIndexChanged(std::function<void(int)> cb) {
    on_index_changed_ = std::move(cb);
}

void Carousel::GoTo(int index, bool animate) {
    const int n = static_cast<int>(slides_.size());
    if (n == 0) return;
    if (loop_) {
        index = ((index % n) + n) % n;
    } else {
        index = std::clamp(index, 0, n - 1);
    }
    if (index == index_ && animate) return;

    const bool changed = index != index_;
    index_ = index;
    const float w = GetBounds().width;
    target_scroll_ = static_cast<float>(index_) * w;
    if (animate) {
        // Tween from current scroll position toward the new target.
        scroll_t_.from = scroll_;
        scroll_t_.to = target_scroll_;
        scroll_t_.t = 0.0f;
        scroll_t_.duration = 0.28f;
    } else {
        scroll_ = target_scroll_;
        scroll_t_.from = scroll_t_.to = scroll_;
        scroll_t_.t = 1.0f;
    }
    if (changed && on_index_changed_) on_index_changed_(index_);
    RequestLayout();
    Invalidate();
}

Layout::Rect Carousel::ArrowRect(bool next,
                                 const Layout::Rect& b) const {
    const float size = 32.0f;
    Layout::Rect r;
    r.width = r.height = size;
    r.y = b.y + b.height * 0.5f - size * 0.5f;
    r.x = next ? b.x + b.width - size - 8.0f : b.x + 8.0f;
    return r;
}

Layout::Rect Carousel::DotsRect(const Layout::Rect& b) const {
    const int n = static_cast<int>(slides_.size());
    const float dot = 7.0f, gap = 7.0f;
    const float w = n * dot + (n - 1) * gap;
    return {b.x + b.width * 0.5f - w * 0.5f, b.y + b.height - 20.0f, w, dot};
}

void Carousel::Layout(const Layout::Rect& bounds) {
    SetBounds(bounds);
    const int n = static_cast<int>(slides_.size());
    if (n == 0) return;

    // Slides are full-width strips positioned by scroll offset.
    const float w = bounds.width;
    const float content_h = std::max(0.0f, bounds.height - (show_dots_ ? 24.0f : 0.0f));
    for (size_t i = 0; i < slides_.size(); ++i) {
        Layout::Rect r{bounds.x + static_cast<float>(i) * w - scroll_,
                       bounds.y, w, content_h};
        slides_[i]->SetBounds(r);
        slides_[i]->Layout(r, CachedLayoutTokens());
    }
    if (show_arrows_) {
        prev_rect_ = ArrowRect(false, bounds);
        next_rect_ = ArrowRect(true, bounds);
    }
    if (show_dots_) dots_rect_ = DotsRect(bounds);
}

void Carousel::Render(Rendering::Renderer& renderer,
                      const Theme::Tokens& tokens) {
    const auto b = GetBounds();
    if (b.width <= 0.0f || b.height <= 0.0f) return;

    // Slides render via RenderSubtree. Arrows + dots on top.
    if (show_arrows_) {
        for (bool next : {false, true}) {
            const auto& r = next ? next_rect_ : prev_rect_;
            renderer.FillRect(r, tokens.surface, r.width * 0.5f);
            renderer.StrokeRect(r, tokens.border, 1.0f, r.width * 0.5f);
            // Chevron glyph.
            const float cx = r.x + r.width * 0.5f;
            const float cy = r.y + r.height * 0.5f;
            const float s = 4.0f;
            const float dir = next ? 1.0f : -1.0f;
            renderer.DrawLine({cx - s * dir, cy - s},
                              {cx + s * dir, cy}, tokens.foreground, 1.4f);
            renderer.DrawLine({cx + s * dir, cy},
                              {cx - s * dir, cy + s}, tokens.foreground, 1.4f);
        }
    }
    if (show_dots_) {
        const int n = static_cast<int>(slides_.size());
        const float dot = 7.0f, gap = 7.0f;
        for (int i = 0; i < n; ++i) {
            const float dx = dots_rect_.x + i * (dot + gap);
            const Theme::Color c =
                i == index_ ? tokens.accent : tokens.muted;
            Layout::Rect dr{dx, dots_rect_.y, dot, dot};
            renderer.FillRect(dr, c, dot * 0.5f);
        }
    }
}

void Carousel::Tick(double dt_seconds) {
    if (!IsEffectivelyVisible()) return;
    scroll_ = scroll_t_.Update(static_cast<float>(dt_seconds));
    Invalidate();
}

bool Carousel::HandleEvent(const Event& event) {
    const auto& p = event.position;
    switch (event.type) {
        case Event::Type::Click:
            if (show_arrows_) {
                if (prev_rect_.width > 0.0f &&
                    p.x >= prev_rect_.x && p.x < prev_rect_.x + prev_rect_.width &&
                    p.y >= prev_rect_.y && p.y < prev_rect_.y + prev_rect_.height) {
                    Prev();
                    return true;
                }
                if (next_rect_.width > 0.0f &&
                    p.x >= next_rect_.x && p.x < next_rect_.x + next_rect_.width &&
                    p.y >= next_rect_.y && p.y < next_rect_.y + next_rect_.height) {
                    Next();
                    return true;
                }
            }
            return false;
        case Event::Type::MouseDown:
            if (event.mouse_button == MouseButton::Left) {
                dragging_ = true;
                drag_start_x_ = p.x;
                drag_start_scroll_ = scroll_;
                // Stop any in-flight tween so Tick() doesn't fight the drag.
                scroll_t_.from = scroll_t_.to = scroll_;
                scroll_t_.t = 1.0f;
                return true;
            }
            return false;
        case Event::Type::MouseMove:
            if (dragging_) {
                scroll_ = drag_start_scroll_ - (p.x - drag_start_x_);
                RequestLayout();
                Invalidate();
                return true;
            }
            return false;
        case Event::Type::MouseUp:
            if (dragging_) {
                dragging_ = false;
                const float w = GetBounds().width;
                const int n = static_cast<int>(slides_.size());
                int target = index_;
                if (w > 0.0f) {
                    target = static_cast<int>(
                        std::lround(scroll_ / w));
                }
                // Drag right (negative scroll) → previous.
                target = std::clamp(target, 0, n - 1);
                if (loop_) {
                    // Wrap: allow one step past either end.
                    if (scroll_ < -w * 0.25f) target = index_ - 1;
                    else if (scroll_ > w * 1.25f) target = index_ + 1;
                }
                GoTo(target, true);
                return true;
            }
            return false;
        case Event::Type::KeyDown:
            if (event.key == KeyCode::ArrowLeft) { Prev(); return true; }
            if (event.key == KeyCode::ArrowRight) { Next(); return true; }
            return false;
        default:
            return false;
    }
}

Layout::Size Carousel::GetMinSize() const {
    return {120.0f, 80.0f};
}

Layout::Size Carousel::GetPreferredSize() const {
    return {640.0f, 360.0f};
}

Layout::Size Carousel::GetMaxSize() const {
    return {100000.0f, 100000.0f};
}

} // namespace XER::UI::Widgets
