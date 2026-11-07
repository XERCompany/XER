#include "slider.h"

#include <algorithm>
#include <utility>

#include "motion/motion.h"
#include "rendering/renderer.h"
#include "theme/tokens.h"

namespace XER::UI::Widgets {
namespace {

constexpr float kThumbDiameter = 16.0f;
constexpr float kTrackHeight = 4.0f;
constexpr float kArrowStepFraction = 0.01f;

} // namespace

Slider::Slider(float minimum, float maximum, float value)
    : minimum_(minimum), maximum_(maximum), value_(minimum) {
    if (minimum_ > maximum_) {
        std::swap(minimum_, maximum_);
    }
    SetFocusable(true);
    SetValue(value);
}

void Slider::SetRange(float minimum, float maximum) {
    if (minimum > maximum) {
        std::swap(minimum, maximum);
    }
    minimum_ = minimum;
    maximum_ = maximum;
    SetValue(value_);
}

void Slider::SetValue(float value) {
    const float clamped = std::clamp(value, minimum_, maximum_);
    if (clamped == value_) {
        return;
    }
    value_ = clamped;
    Invalidate();
    if (on_value_changed_) {
        on_value_changed_(value_);
    }
}

float Slider::GetValue() const {
    return value_;
}

bool Slider::IsDragging() const {
    return dragging_;
}

void Slider::SetOnValueChanged(std::function<void(float)> callback) {
    on_value_changed_ = std::move(callback);
}

void Slider::Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) {
    const auto bounds = GetBounds();
    const float center_y = bounds.y + bounds.height / 2.0f;
    const float thumb_radius = kThumbDiameter / 2.0f;
    const float track_left = bounds.x + thumb_radius;
    const float track_right = std::max(track_left, bounds.x + bounds.width - thumb_radius);
    const float track_width = track_right - track_left;
    const float ratio = maximum_ > minimum_ ? (value_ - minimum_) / (maximum_ - minimum_) : 0.0f;
    const float thumb_x = track_left + track_width * ratio;
    const float track_y = center_y - kTrackHeight / 2.0f;

    renderer.FillRect(
        {track_left, track_y, track_width, kTrackHeight},
        tokens.secondary,
        kTrackHeight / 2.0f
    );
    if (ratio > 0.0f) {
        renderer.FillRect(
            {track_left, track_y, track_width * ratio, kTrackHeight},
            IsEnabled() ? tokens.foreground : tokens.muted,
            kTrackHeight / 2.0f
        );
    }

    const float swell = swell_.Value();
    const float thumb_d = kThumbDiameter + swell * 3.0f +
        (IsHovered() || dragging_ ? 1.0f : 0.0f);
    const Layout::Rect thumb{
        thumb_x - thumb_d / 2.0f,
        center_y - thumb_d / 2.0f,
        thumb_d,
        thumb_d
    };
    renderer.FillRect(
        thumb,
        IsEnabled() ? tokens.surface : tokens.muted,
        thumb_d / 2.0f
    );
    renderer.StrokeRect(
        thumb,
        IsEnabled() ? tokens.foreground : tokens.border,
        2.0f,
        thumb_d / 2.0f
    );

    if (IsFocused()) {
        constexpr float kRingOffset = 2.0f;
        renderer.StrokeRect(
            Layout::Rect{
                thumb.x - kRingOffset,
                thumb.y - kRingOffset,
                thumb.width + 2.0f * kRingOffset,
                thumb.height + 2.0f * kRingOffset
            },
            tokens.ring,
            2.0f,
            thumb_d / 2.0f + kRingOffset
        );
    }
}

void Slider::Tick(double dt_seconds) {
    if (!IsEffectivelyVisible()) {
        return;
    }
    const float want = (IsHovered() && IsEnabled()) || dragging_ ? 1.0f : 0.0f;
    if (swell_.to != want) {
        swell_.Set(want, 0.12f);
    }
    swell_.Update(static_cast<float>(dt_seconds));
    if (swell_.Active()) {
        Invalidate();
    }
}

void Slider::Layout(const Layout::Rect& bounds) {
    SetBounds(bounds);
}

bool Slider::HandleEvent(const Event& event) {
    if (!IsEnabled()) {
        return false;
    }

    if (event.type == Event::Type::KeyDown && IsFocused()) {
        const float step = (maximum_ - minimum_) * kArrowStepFraction;
        switch (event.key) {
        case KeyCode::ArrowLeft:
        case KeyCode::ArrowDown:
            SetValue(value_ - step);
            return true;
        case KeyCode::ArrowRight:
        case KeyCode::ArrowUp:
            SetValue(value_ + step);
            return true;
        default:
            break;
        }
    }

    if (event.type == Event::Type::Blur) {
        dragging_ = false;
        return false;
    }
    if (event.type == Event::Type::MouseMove && dragging_ &&
        event.mouse_button == MouseButton::Left) {
        SetValueFromPosition(event.position.x);
        return true;
    }
    if (event.type == Event::Type::MouseUp && dragging_ &&
        event.mouse_button == MouseButton::Left) {
        dragging_ = false;
        Invalidate();
        return true;
    }
    if (event.type == Event::Type::MouseDown &&
        event.mouse_button == MouseButton::Left) {
        dragging_ = true;
        SetValueFromPosition(event.position.x);
        return true;
    }
    if (event.type == Event::Type::Click && event.mouse_button == MouseButton::Left) {
        SetValueFromPosition(event.position.x);
        return true;
    }
    return false;
}

Layout::Size Slider::GetMinSize() const {
    return {40.0f, 20.0f};
}

Layout::Size Slider::GetPreferredSize() const {
    return {120.0f, 24.0f};
}

Layout::Size Slider::GetMaxSize() const {
    return {100000.0f, 100000.0f};
}

void Slider::SetValueFromPosition(float x) {
    const auto bounds = GetBounds();
    const float thumb_radius = kThumbDiameter / 2.0f;
    const float track_left = bounds.x + thumb_radius;
    const float track_right = std::max(track_left, bounds.x + bounds.width - thumb_radius);
    const float track_width = track_right - track_left;
    if (track_width <= 0.0f || maximum_ <= minimum_) {
        SetValue(minimum_);
        return;
    }
    const float ratio = std::clamp((x - track_left) / track_width, 0.0f, 1.0f);
    SetValue(minimum_ + ratio * (maximum_ - minimum_));
}

} // namespace XER::UI::Widgets
