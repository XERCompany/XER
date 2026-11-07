#include "checkbox.h"

#include <algorithm>
#include <utility>

#include "motion/motion.h"
#include "rendering/renderer.h"
#include "theme/tokens.h"

namespace XER::UI::Widgets {

Checkbox::Checkbox(bool checked) : checked_(checked) {
    SetFocusable(true);
    check_t_.Set(checked_ ? 1.0f : 0.0f);
}

bool Checkbox::IsChecked() const {
    return checked_;
}

void Checkbox::SetChecked(bool checked) {
    if (checked_ == checked) {
        return;
    }
    checked_ = checked;
    // Draw-on is a touch slower than draw-off (exits quicker than entrances).
    check_t_.Set(checked_ ? 1.0f : 0.0f, checked_ ? 0.18f : 0.10f);
    Invalidate();
    if (on_changed_) {
        on_changed_(checked_);
    }
}

void Checkbox::SetOnChanged(std::function<void(bool)> callback) {
    on_changed_ = std::move(callback);
}

void Checkbox::Tick(double dt_seconds) {
    if (!IsEffectivelyVisible()) {
        return;
    }
    check_t_.Update(static_cast<float>(dt_seconds));
    if (check_t_.Active()) {
        Invalidate();
    }
}

void Checkbox::Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) {
    const auto bounds = GetBounds();
    const float box_size = 16.0f;
    const Layout::Rect box{
        bounds.x + (bounds.width - box_size) / 2.0f,
        bounds.y + (bounds.height - box_size) / 2.0f,
        box_size,
        box_size
    };
    const float check = check_t_.Value();

    Theme::Color fill_color = tokens.surface;
    Theme::Color stroke_color = tokens.border;
    Theme::Color check_color = tokens.foreground;
    if (!IsEnabled()) {
        fill_color = tokens.muted;
        stroke_color = tokens.border;
        check_color = tokens.background;
    } else if (check > 0.5f) {
        // Tailwind accent checkbox: indigo fill, white tick.
        fill_color = tokens.accent;
        check_color = tokens.accent_foreground;
    } else if (check > 0.01f) {
        // Mid-draw: blend toward the checked fill so the box doesn't pop.
        fill_color = Motion::LerpColor(tokens.surface, tokens.accent, check);
        check_color = Motion::LerpColor(tokens.foreground, tokens.accent_foreground, check);
    }
    if (IsHovered() && IsEnabled() && check < 0.5f) {
        fill_color = Motion::LerpColor(fill_color, tokens.hover, 0.6f);
    }

    renderer.FillRect(box, fill_color, tokens.radius_small);
    renderer.StrokeRect(box, stroke_color, 1.0f, tokens.radius_small);
    if (check > 0.01f) {
        // The tick draws itself: segment 1 leads, segment 2 follows.
        const Layout::Point p1{box.x + 3.5f, box.y + 8.0f};
        const Layout::Point p2{box.x + 6.5f, box.y + 11.0f};
        const Layout::Point p3{box.x + 12.5f, box.y + 4.5f};
        const float s1 = std::clamp(check * 2.0f, 0.0f, 1.0f);
        const float s2 = std::clamp(check * 2.0f - 1.0f, 0.0f, 1.0f);
        renderer.DrawLine(p1,
                          {p1.x + (p2.x - p1.x) * s1, p1.y + (p2.y - p1.y) * s1},
                          check_color, 2.0f);
        if (s2 > 0.0f) {
            renderer.DrawLine(
                p2,
                {p2.x + (p3.x - p2.x) * s2, p2.y + (p3.y - p2.y) * s2},
                check_color, 2.0f);
        }
    }

    if (IsFocused()) {
        constexpr float kRingOffset = 2.0f;
        renderer.StrokeRect(
            Layout::Rect{
                box.x - kRingOffset,
                box.y - kRingOffset,
                box.width + 2.0f * kRingOffset,
                box.height + 2.0f * kRingOffset
            },
            tokens.ring,
            2.0f,
            tokens.radius_small + kRingOffset
        );
    }
}

void Checkbox::Layout(const Layout::Rect& bounds) {
    SetBounds(bounds);
}

bool Checkbox::HandleEvent(const Event& event) {
    if (!IsEnabled()) {
        return false;
    }

    if (event.type == Event::Type::Click && event.mouse_button == MouseButton::Left) {
        Toggle();
        return true;
    }
    if (event.type == Event::Type::KeyDown && IsFocused() &&
        event.key == KeyCode::Space) {
        Toggle();
        return true;
    }
    return false;
}

Layout::Size Checkbox::GetMinSize() const {
    return {16.0f, 16.0f};
}

Layout::Size Checkbox::GetPreferredSize() const {
    return {16.0f, 16.0f};
}

Layout::Size Checkbox::GetMaxSize() const {
    return {16.0f, 16.0f};
}

void Checkbox::Toggle() {
    checked_ = !checked_;
    check_t_.Set(checked_ ? 1.0f : 0.0f, checked_ ? 0.18f : 0.10f);
    Invalidate();
    if (on_changed_) {
        on_changed_(checked_);
    }
}

} // namespace XER::UI::Widgets
