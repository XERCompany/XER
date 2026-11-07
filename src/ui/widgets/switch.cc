#include "switch.h"

#include <utility>

#include "motion/motion.h"
#include "rendering/renderer.h"
#include "theme/tokens.h"

namespace XER::UI::Widgets {

Switch::Switch(bool checked) : checked_(checked) {
    SetFocusable(true);
    thumb_t_.Snap(checked_ ? 1.0f : 0.0f);
    thumb_t_.stiffness = 420.0f;
    thumb_t_.damping = 32.0f;
    press_t_.Set(0.0f);
}

bool Switch::IsChecked() const {
    return checked_;
}

void Switch::SetChecked(bool checked) {
    if (checked_ == checked) {
        return;
    }
    checked_ = checked;
    thumb_t_.SetTarget(checked_ ? 1.0f : 0.0f);
    Invalidate();
    if (on_changed_) {
        on_changed_(checked_);
    }
}

void Switch::SetOnChanged(std::function<void(bool)> callback) {
    on_changed_ = std::move(callback);
}

void Switch::Tick(double dt_seconds) {
    if (!IsEffectivelyVisible()) {
        return;
    }
    const float want_press = held_ ? 1.0f : 0.0f;
    if (press_t_.to != want_press) {
        press_t_.Set(want_press, 0.10f);
    }
    press_t_.Update(static_cast<float>(dt_seconds));
    thumb_t_.Update(static_cast<float>(dt_seconds));
    if (!thumb_t_.Settled() || press_t_.Active()) {
        Invalidate();
    }
}

void Switch::Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) {
    const auto bounds = GetBounds();
    const float radius = bounds.height / 2.0f;
    const float t = thumb_t_.pos;
    const float press = press_t_.Value();

    const auto lerp = [](const Theme::Color& a, const Theme::Color& b, float t) {
        return Motion::LerpColor(a, b, t);
    };

    // Track: off = quiet hover-gray, on = accent (Tailwind bg-indigo-600).
    Theme::Color track_off = tokens.hover;
    Theme::Color track_on = tokens.accent;
    Theme::Color track_color = lerp(track_off, track_on, t);
    if (!IsEnabled()) {
        track_color = tokens.muted;
    } else if (!checked_ && t < 0.05f && IsHovered()) {
        track_color = tokens.foreground;
        track_color.a = 0.12f;
    }

    renderer.FillRect(bounds, track_color, radius);
    if (IsFocused()) {
        constexpr float kRingOffset = 2.0f;
        renderer.StrokeRect(
            Layout::Rect{
                bounds.x - kRingOffset,
                bounds.y - kRingOffset,
                bounds.width + 2.0f * kRingOffset,
                bounds.height + 2.0f * kRingOffset
            },
            tokens.ring,
            2.0f,
            radius + kRingOffset
        );
    } else if (t < 0.5f && IsEnabled()) {
        renderer.StrokeRect(bounds, tokens.border, 1.0f, radius);
    }

    const float inset = 2.0f;
    const float diameter = bounds.height - 2.0f * inset;
    const float travel = bounds.width - diameter - 2.0f * inset;
    const float thumb_cx = bounds.x + inset + diameter * 0.5f + travel * t;
    // Stretch while held: the thumb widens ~30% around its center.
    const float stretch = 1.0f + 0.30f * press;
    const float thumb_w = diameter * stretch;
    const float thumb_x = thumb_cx - thumb_w * 0.5f;
    renderer.FillRect(
        {thumb_x, bounds.y + inset, thumb_w, diameter},
        IsEnabled() ? tokens.surface : tokens.background,
        diameter / 2.0f
    );
}

void Switch::Layout(const Layout::Rect& bounds) {
    SetBounds(bounds);
}

bool Switch::HandleEvent(const Event& event) {
    if (!IsEnabled()) {
        return false;
    }

    switch (event.type) {
        case Event::Type::MouseDown:
            if (event.mouse_button == MouseButton::Left) {
                held_ = true;
                press_t_.Set(1.0f, 0.08f);
                Invalidate();
                return true;
            }
            return false;
        case Event::Type::MouseUp:
            if (held_ && event.mouse_button == MouseButton::Left) {
                held_ = false;
                press_t_.Set(0.0f, 0.12f);
                Invalidate();
                return true;
            }
            return false;
        case Event::Type::Click:
            if (event.mouse_button == MouseButton::Left) {
                held_ = false;
                press_t_.Set(0.0f, 0.12f);
                Toggle();
                return true;
            }
            return false;
        case Event::Type::KeyDown:
            if (IsFocused() && event.key == KeyCode::Space) {
                Toggle();
                return true;
            }
            return false;
        default:
            return false;
    }
}

Layout::Size Switch::GetMinSize() const {
    return {36.0f, 20.0f};
}

Layout::Size Switch::GetPreferredSize() const {
    return {36.0f, 20.0f};
}

Layout::Size Switch::GetMaxSize() const {
    return {36.0f, 20.0f};
}

void Switch::Toggle() {
    checked_ = !checked_;
    thumb_t_.SetTarget(checked_ ? 1.0f : 0.0f);
    Invalidate();
    if (on_changed_) {
        on_changed_(checked_);
    }
}

} // namespace XER::UI::Widgets
