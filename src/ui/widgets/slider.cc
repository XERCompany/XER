#include "slider.h"

#include <algorithm>
#include <utility>

#include "rendering/renderer.h"
#include "theme/tokens.h"

namespace XER::UI::Widgets {
namespace {

constexpr float kThumbRadius = 6.0f;
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

void Slider::SetOnValueChanged(std::function<void(float)> callback) {
    on_value_changed_ = std::move(callback);
}

void Slider::Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) {
    const auto bounds = GetBounds();
    const float center_y = bounds.y + bounds.height / 2.0f;
    const float track_left = bounds.x + kThumbRadius;
    const float track_right = std::max(track_left, bounds.x + bounds.width - kThumbRadius);
    const float track_width = track_right - track_left;
    const float ratio = maximum_ > minimum_ ? (value_ - minimum_) / (maximum_ - minimum_) : 0.0f;
    const float thumb_x = track_left + track_width * ratio;

    renderer.FillRect(
        {track_left, center_y - 2.0f, track_width, 4.0f},
        tokens.muted,
        tokens.radius_small
    );
    renderer.FillRect(
        {track_left, center_y - 2.0f, track_width * ratio, 4.0f},
        tokens.accent,
        tokens.radius_small
    );
    renderer.FillRect(
        {thumb_x - kThumbRadius, center_y - kThumbRadius, kThumbRadius * 2.0f, kThumbRadius * 2.0f},
        IsEnabled() ? tokens.surface : tokens.muted,
        kThumbRadius
    );
    renderer.StrokeRect(
        {thumb_x - kThumbRadius, center_y - kThumbRadius, kThumbRadius * 2.0f, kThumbRadius * 2.0f},
        IsFocused() ? tokens.focus : tokens.border,
        1.0f,
        kThumbRadius
    );
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

    if ((event.type == Event::Type::MouseDown || event.type == Event::Type::Click) &&
        event.mouse_button == MouseButton::Left) {
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
    const float track_left = bounds.x + kThumbRadius;
    const float track_right = std::max(track_left, bounds.x + bounds.width - kThumbRadius);
    const float track_width = track_right - track_left;
    if (track_width <= 0.0f || maximum_ <= minimum_) {
        SetValue(minimum_);
        return;
    }
    const float ratio = std::clamp((x - track_left) / track_width, 0.0f, 1.0f);
    SetValue(minimum_ + ratio * (maximum_ - minimum_));
}

} // namespace XER::UI::Widgets
