#include "knob.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

#include "rendering/renderer.h"
#include "theme/tokens.h"

namespace XER::UI::Widgets {
namespace {

constexpr float kPi = 3.14159265358979323846f;
// Arc geometry: 270deg sweep, gap at the bottom. Angles are clockwise
// degrees from 12 o'clock: 0=up, 90=right, 180=down, 270=left. The arc
// starts at 225 (bottom-left) and ends at 495 (=135, bottom-right).
constexpr float kArcStartDeg = 225.0f;
constexpr float kArcSweepDeg = 270.0f;

constexpr auto kDoubleClickWindow = std::chrono::milliseconds(450);
constexpr float kDoubleClickRadius = 12.0f;
// Wheel/keyboard nudges coalesce into one committed value after this much
// idle tick-time (accumulated dt), matching "committed = gesture end".
constexpr double kCommitDebounceSeconds = 0.4;
// Shift during a drag = 4x slower (audio-ui knob convention).
constexpr float kShiftDragScale = 0.25f;
// Detent snap zone: pull-strength 1.0 grabs +/-4% of the range.
constexpr float kDetentZoneFraction = 0.04f;

// Vertical space reserved under the dial: value readout + label line.
constexpr float kValueLineH = 16.0f;
constexpr float kLabelLineH = 15.0f;
constexpr float kTextGap = 2.0f;

Layout::Point PointOnCircle(float cx, float cy, float radius,
                            float angle_deg) {
    const float rad = angle_deg * kPi / 180.0f;
    return {cx + radius * std::sin(rad), cy - radius * std::cos(rad)};
}

} // namespace

std::shared_ptr<Knob> Knob::Create() {
    return std::shared_ptr<Knob>(new Knob());
}

Knob::Knob() {
    SetFocusable(true);
}

float Knob::NormalizedToValue(float t) const {
    t = std::clamp(t, 0.0f, 1.0f);
    if (taper_to_value_) {
        return taper_to_value_(t);
    }
    if (scale_ == Scale::Log && minimum_ > 0.0f && maximum_ > minimum_) {
        return minimum_ * std::pow(maximum_ / minimum_, t);
    }
    return minimum_ + t * (maximum_ - minimum_);
}

float Knob::ValueToNormalized(float value) const {
    if (taper_to_normalized_) {
        return std::clamp(taper_to_normalized_(value), 0.0f, 1.0f);
    }
    if (scale_ == Scale::Log && minimum_ > 0.0f && maximum_ > minimum_) {
        const float v = std::clamp(value, minimum_, maximum_);
        return std::clamp(std::log(v / minimum_) /
                              std::log(maximum_ / minimum_),
                          0.0f, 1.0f);
    }
    if (maximum_ <= minimum_) {
        return 0.0f;
    }
    return std::clamp((value - minimum_) / (maximum_ - minimum_),
                      0.0f, 1.0f);
}

void Knob::SetValue(float value) {
    const float clamped = std::clamp(value, minimum_, maximum_);
    if (clamped == value_) {
        return;
    }
    value_ = clamped;
    Invalidate();
    if (on_value_change_) {
        on_value_change_(value_);
    }
}

void Knob::SetNormalizedValue(float t) {
    SetValue(NormalizedToValue(t));
}

float Knob::GetNormalizedValue() const {
    return ValueToNormalized(value_);
}

void Knob::SetRange(float minimum, float maximum) {
    if (minimum > maximum) {
        std::swap(minimum, maximum);
    }
    minimum_ = minimum;
    maximum_ = maximum;
    anchor_ = std::clamp(anchor_, minimum_, maximum_);
    SetValue(value_);
}

void Knob::SetScale(Scale scale) {
    if (scale_ == scale) {
        return;
    }
    scale_ = scale;
    Invalidate();
}

void Knob::SetTaper(std::function<float(float)> normalized_to_value,
                    std::function<float(float)> value_to_normalized) {
    taper_to_value_ = std::move(normalized_to_value);
    taper_to_normalized_ = std::move(value_to_normalized);
    Invalidate();
}

void Knob::ClearTaper() {
    taper_to_value_ = nullptr;
    taper_to_normalized_ = nullptr;
    Invalidate();
}

void Knob::SetDragMode(DragMode mode) {
    drag_mode_ = mode;
}

void Knob::SetVerticalSensitivity(float px_per_range) {
    if (px_per_range > 0.0f) {
        vertical_sensitivity_ = px_per_range;
    }
}

void Knob::SetAnchor(float value) {
    anchor_ = std::clamp(value, minimum_, maximum_);
    Invalidate();
}

void Knob::SetDetents(std::vector<float> values) {
    detents_ = std::move(values);
    Invalidate();
}

void Knob::SetDetentPull(float strength) {
    detent_pull_ = std::clamp(strength, 0.0f, 1.0f);
}

void Knob::SetStep(float normalized_step) {
    if (normalized_step > 0.0f) {
        step_ = normalized_step;
    }
}

void Knob::SetFineStep(float normalized_step) {
    if (normalized_step > 0.0f) {
        fine_step_ = normalized_step;
    }
}

void Knob::SetLargeStep(float normalized_step) {
    if (normalized_step > 0.0f) {
        large_step_ = normalized_step;
    }
}

void Knob::SetResetValue(float value) {
    reset_value_ = std::clamp(value, minimum_, maximum_);
}

void Knob::ClearResetValue() {
    reset_value_.reset();
}

void Knob::SetLabel(std::string text) {
    if (label_ == text) {
        return;
    }
    label_ = std::move(text);
    Invalidate();
    RequestLayout();
}

void Knob::SetUnit(std::string suffix) {
    if (unit_ == suffix) {
        return;
    }
    unit_ = std::move(suffix);
    Invalidate();
}

void Knob::SetShowTicks(bool show) {
    if (show_ticks_ == show) {
        return;
    }
    show_ticks_ = show;
    Invalidate();
}

void Knob::SetValueFormatter(
    std::function<std::string(float)> formatter) {
    value_formatter_ = std::move(formatter);
    Invalidate();
}

std::string Knob::FormatValue(float value) const {
    if (value_formatter_) {
        return value_formatter_(value);
    }
    char buf[64];
    const int n = std::snprintf(buf, sizeof(buf), "%.4g",
                                static_cast<double>(value));
    std::string out = n > 0 ? std::string(buf, static_cast<std::size_t>(n))
                            : std::string();
    if (!unit_.empty()) {
        if (!out.empty()) {
            out += ' ';
        }
        out += unit_;
    }
    return out;
}

void Knob::SetOnValueChange(std::function<void(float)> callback) {
    on_value_change_ = std::move(callback);
}

void Knob::SetOnValueCommitted(std::function<void(float)> callback) {
    on_value_committed_ = std::move(callback);
}

float Knob::AngleForT(float t) {
    return kArcStartDeg + t * kArcSweepDeg;
}

float Knob::TForPointerAngle(float degrees) {
    // The dead zone below the dial (between the two arc ends) clamps to
    // whichever end is nearer — straight down never teleports the value.
    if (degrees > 135.0f && degrees < 225.0f) {
        return degrees <= 180.0f ? 1.0f : 0.0f;
    }
    const float a = degrees < kArcStartDeg ? degrees + 360.0f : degrees;
    return std::clamp((a - kArcStartDeg) / kArcSweepDeg, 0.0f, 1.0f);
}

Layout::Rect Knob::KnobCircle() const {
    const auto bounds = GetBounds();
    const float text_zone = kValueLineH + kTextGap +
        (label_.empty() ? 0.0f : kLabelLineH + kTextGap);
    const float area_h = std::max(0.0f, bounds.height - text_zone);
    const float d = std::max(0.0f,
        std::min(bounds.width, area_h) - 8.0f);
    const float cx = bounds.x + bounds.width / 2.0f;
    const float cy = bounds.y + area_h / 2.0f;
    return {cx - d / 2.0f, cy - d / 2.0f, d, d};
}

float Knob::PointerAngle(const Layout::Point& position) const {
    const auto circle = KnobCircle();
    const float dx = position.x - (circle.x + circle.width / 2.0f);
    const float dy = position.y - (circle.y + circle.height / 2.0f);
    float deg = std::atan2(dx, -dy) * 180.0f / kPi;
    if (deg < 0.0f) {
        deg += 360.0f;
    }
    return deg;
}

void Knob::ApplyGestureNormalized(float t) {
    float value = NormalizedToValue(t);
    // Detent pull: within the zone the detent wins outright — the
    // "notch" is a hard snap, strength scales the zone width.
    if (!detents_.empty() && detent_pull_ > 0.0f) {
        const float zone =
            detent_pull_ * kDetentZoneFraction * (maximum_ - minimum_);
        float best = 0.0f;
        float best_dist = zone;
        bool found = false;
        for (const float d : detents_) {
            const float dist = std::fabs(value - d);
            if (dist <= best_dist) {
                best = d;
                best_dist = dist;
                found = true;
            }
        }
        if (found) {
            value = best;
        }
    }
    const float clamped = std::clamp(value, minimum_, maximum_);
    if (clamped == value_) {
        return;
    }
    value_ = clamped;
    Invalidate();
    if (on_value_change_) {
        on_value_change_(value_);
    }
    pending_commit_ = true;
    commit_idle_ = 0.0;
}

void Knob::CommitGesture() {
    if (!pending_commit_) {
        return;
    }
    pending_commit_ = false;
    if (on_value_committed_) {
        on_value_committed_(value_);
    }
}

void Knob::Reset() {
    if (!reset_value_.has_value()) {
        return;
    }
    ApplyGestureNormalized(ValueToNormalized(*reset_value_));
    CommitGesture();
}

void Knob::StrokeArc(Rendering::Renderer& renderer, float cx, float cy,
                     float radius, float a0, float a1, Theme::Color color,
                     float width) const {
    const float span = a1 - a0;
    const int segments =
        std::max(1, static_cast<int>(std::ceil(std::fabs(span) / 4.0f)));
    Layout::Point prev = PointOnCircle(cx, cy, radius, a0);
    for (int i = 1; i <= segments; ++i) {
        const float a = a0 + span * static_cast<float>(i) /
                          static_cast<float>(segments);
        const Layout::Point next = PointOnCircle(cx, cy, radius, a);
        renderer.DrawLine(prev, next, color, width);
        prev = next;
    }
}

void Knob::Render(Rendering::Renderer& renderer,
                  const Theme::Tokens& tokens) {
    const auto bounds = GetBounds();
    const bool enabled = IsEnabled();
    const auto circle = KnobCircle();
    const float cx = circle.x + circle.width / 2.0f;
    const float cy = circle.y + circle.height / 2.0f;
    const float r = circle.width / 2.0f;
    if (r <= 0.0f) {
        return;
    }

    const float value_t = ValueToNormalized(value_);
    const float anchor_t = ValueToNormalized(anchor_);
    const Theme::Color track = enabled ? tokens.secondary : tokens.border;
    const Theme::Color accent = enabled ? tokens.accent : tokens.muted;

    // Tick ring: 11 graduations; those between anchor and value light up
    // (bipolar knobs light from the center outward).
    if (show_ticks_ && r > 12.0f) {
        constexpr int kTicks = 11;
        const float lo = std::min(anchor_t, value_t);
        const float hi = std::max(anchor_t, value_t);
        for (int i = 0; i < kTicks; ++i) {
            const float t = static_cast<float>(i) / (kTicks - 1);
            const float a = AngleForT(t);
            const bool lit = t >= lo - 0.0001f && t <= hi + 0.0001f;
            const Theme::Color tick_color =
                lit ? accent : (enabled ? tokens.muted : tokens.disabled);
            renderer.DrawLine(
                PointOnCircle(cx, cy, r + 2.5f, a),
                PointOnCircle(cx, cy, r + 6.0f, a),
                tick_color, 1.2f);
        }
    }

    // Track arc, then the value arc growing from the anchor.
    StrokeArc(renderer, cx, cy, r, kArcStartDeg,
              kArcStartDeg + kArcSweepDeg, track, 3.0f);
    const float anchor_a = AngleForT(anchor_t);
    const float value_a = AngleForT(value_t);
    if (std::fabs(value_a - anchor_a) > 0.5f) {
        StrokeArc(renderer, cx, cy, r, anchor_a, value_a, accent, 3.0f);
    }

    // Detent markers sit on the track arc.
    if (!detents_.empty()) {
        for (const float d : detents_) {
            const float a = AngleForT(ValueToNormalized(d));
            const auto p = PointOnCircle(cx, cy, r, a);
            renderer.FillRect({p.x - 1.5f, p.y - 1.5f, 3.0f, 3.0f},
                              enabled ? tokens.muted : tokens.disabled,
                              1.5f);
        }
    }

    // Cap + thumb indicator.
    const float cap_r = r - 4.5f;
    const float glow = hover_glow_.Value();
    Theme::Color cap_border = enabled ? tokens.border : tokens.disabled;
    if (glow > 0.001f && enabled) {
        cap_border = Motion::LerpColor(cap_border, tokens.accent, glow);
    }
    renderer.FillRect({cx - cap_r, cy - cap_r, cap_r * 2.0f, cap_r * 2.0f},
                      enabled ? tokens.surface : tokens.secondary, cap_r);
    renderer.StrokeRect({cx - cap_r, cy - cap_r, cap_r * 2.0f,
                         cap_r * 2.0f},
                        cap_border, 1.5f, cap_r);
    const auto thumb_from = PointOnCircle(cx, cy, cap_r * 0.35f, value_a);
    const auto thumb_to = PointOnCircle(cx, cy, cap_r - 1.0f, value_a);
    renderer.DrawLine(thumb_from, thumb_to,
                      enabled ? (dragging_ ? tokens.accent
                                           : tokens.foreground)
                              : tokens.disabled,
                      2.5f);

    // Readout + label under the dial.
    const float text_top =
        circle.y + circle.height + kTextGap;
    const std::string readout = FormatValue(value_);
    if (!readout.empty()) {
        const Rendering::TextStyle value_style{
            enabled ? tokens.timecode : tokens.disabled,
            tokens.font_small, 500, true};
        const auto size = renderer.MeasureText(readout, value_style);
        renderer.DrawText(
            readout,
            {bounds.x + (bounds.width - size.width) / 2.0f, text_top,
             size.width, kValueLineH},
            value_style);
    }
    if (!label_.empty()) {
        const Rendering::TextStyle label_style{
            enabled ? tokens.muted : tokens.disabled,
            tokens.font_small, 400};
        const auto size = renderer.MeasureText(label_, label_style);
        renderer.DrawText(
            label_,
            {bounds.x + (bounds.width - size.width) / 2.0f,
             text_top + kValueLineH + kTextGap,
             size.width, kLabelLineH},
            label_style);
    }

    RenderFocusRing(renderer, tokens, tokens.radius_medium);
}

void Knob::Layout(const Layout::Rect& bounds) {
    SetBounds(bounds);
}

void Knob::Tick(double dt_seconds) {
    if (!IsEffectivelyVisible()) {
        return;
    }
    const float want =
        (IsHovered() && IsEnabled()) || dragging_ ? 1.0f : 0.0f;
    if (hover_glow_.to != want) {
        hover_glow_.Set(want, 0.12f);
    }
    hover_glow_.Update(static_cast<float>(dt_seconds));
    if (hover_glow_.Active()) {
        Invalidate();
    }

    // Widget-focus changes are flag-based (no event) — poll the edge so
    // losing focus lands the pending commit.
    if (was_focused_ && !IsFocused()) {
        dragging_ = false;
        CommitGesture();
    }
    was_focused_ = IsFocused();

    // Debounce is measured in tick time (not wall clock) so a headless
    // Tick() drives it deterministically.
    if (pending_commit_ && !dragging_) {
        commit_idle_ += dt_seconds;
        if (commit_idle_ >= kCommitDebounceSeconds) {
            CommitGesture();
        }
    }
}

bool Knob::HandleEvent(const Event& event) {
    if (!IsEnabled()) {
        return false;
    }

    if (event.type == Event::Type::Blur) {
        dragging_ = false;
        CommitGesture();
        return false;
    }

    switch (event.type) {
    case Event::Type::MouseDown:
        if (event.mouse_button != MouseButton::Left) {
            return false;
        }
        if (event.is_alt && reset_value_.has_value()) {
            just_reset_ = true;
            Reset();
            return true;
        }
        dragging_ = true;
        last_y_ = event.position.y;
        last_angle_ = PointerAngle(event.position);
        return true;

    case Event::Type::MouseMove: {
        if (!dragging_ || event.mouse_button != MouseButton::Left) {
            return false;
        }
        const float fine = event.is_shift ? kShiftDragScale : 1.0f;
        const float t = ValueToNormalized(value_);
        switch (drag_mode_) {
        case DragMode::Vertical: {
            const float dy = last_y_ - event.position.y;
            last_y_ = event.position.y;
            ApplyGestureNormalized(
                t + dy / vertical_sensitivity_ * fine);
            break;
        }
        case DragMode::Circular:
            ApplyGestureNormalized(
                TForPointerAngle(PointerAngle(event.position)));
            break;
        case DragMode::Revolution: {
            const float angle = PointerAngle(event.position);
            float delta = angle - last_angle_;
            // Shortest wrapped angular delta — crossing 0/360 must not
            // swing the knob a full turn backwards.
            while (delta > 180.0f) {
                delta -= 360.0f;
            }
            while (delta < -180.0f) {
                delta += 360.0f;
            }
            last_angle_ = angle;
            ApplyGestureNormalized(t + delta / 360.0f * fine);
            break;
        }
        }
        return true;
    }

    case Event::Type::MouseUp:
        if (event.mouse_button != MouseButton::Left || !dragging_) {
            return false;
        }
        dragging_ = false;
        CommitGesture();
        Invalidate();
        return true;

    case Event::Type::Click: {
        if (event.mouse_button != MouseButton::Left) {
            return false;
        }
        if (just_reset_) {
            just_reset_ = false;
            return true;
        }
        const auto now = std::chrono::steady_clock::now();
        const float dist = std::hypot(
            event.position.x - last_click_pos_.x,
            event.position.y - last_click_pos_.y);
        const bool double_click =
            now - last_click_ < kDoubleClickWindow &&
            dist <= kDoubleClickRadius;
        last_click_ = now;
        last_click_pos_ = event.position;
        if (double_click) {
            last_click_ = std::chrono::steady_clock::time_point{};
            Reset();
        }
        return true;
    }

    case Event::Type::MouseWheel:
        // Wheel only ever acts while focused — a stray scroll over the
        // mixer must not nudge a param.
        if (!IsFocused() || event.wheel_delta == 0) {
            return false;
        }
        {
            const float step =
                event.is_shift ? fine_step_ : step_;
            const float notches =
                static_cast<float>(event.wheel_delta) / 120.0f;
            ApplyGestureNormalized(
                ValueToNormalized(value_) + notches * step);
        }
        return true;

    case Event::Type::KeyDown:
        if (!IsFocused()) {
            return false;
        }
        switch (event.key) {
        case KeyCode::ArrowUp:
        case KeyCode::ArrowRight:
            ApplyGestureNormalized(
                ValueToNormalized(value_) +
                (event.is_shift ? fine_step_ : step_));
            return true;
        case KeyCode::ArrowDown:
        case KeyCode::ArrowLeft:
            ApplyGestureNormalized(
                ValueToNormalized(value_) -
                (event.is_shift ? fine_step_ : step_));
            return true;
        case KeyCode::PageUp:
            ApplyGestureNormalized(
                ValueToNormalized(value_) + large_step_);
            return true;
        case KeyCode::PageDown:
            ApplyGestureNormalized(
                ValueToNormalized(value_) - large_step_);
            return true;
        case KeyCode::Home:
            ApplyGestureNormalized(0.0f);
            CommitGesture();
            return true;
        case KeyCode::End:
            ApplyGestureNormalized(1.0f);
            CommitGesture();
            return true;
        case KeyCode::Delete:
        case KeyCode::Backspace:
            if (reset_value_.has_value()) {
                Reset();
                return true;
            }
            return false;
        case KeyCode::Escape:
            pending_commit_ = false;
            return false;
        default:
            return false;
        }

    default:
        return false;
    }
}

Layout::Size Knob::GetMinSize() const {
    return {40.0f, 56.0f};
}

Layout::Size Knob::GetPreferredSize() const {
    return {64.0f, 86.0f};
}

Layout::Size Knob::GetMaxSize() const {
    return {100000.0f, 100000.0f};
}

std::string Knob::GetDebugText() const {
    return label_.empty() ? FormatValue(value_)
                          : label_ + " " + FormatValue(value_);
}

} // namespace XER::UI::Widgets
