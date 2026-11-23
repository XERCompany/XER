#ifndef XER_UI_WIDGETS_KNOB_H
#define XER_UI_WIDGETS_KNOB_H

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "motion/motion.h"
#include "widgets/widget.h"

namespace XER::UI::Widgets {

// Knob — rotary control for mixer/EQ/FX params (audio-ui + audiocn model).
// ~270° arc: value arc grows from the anchor (center for bipolar pan
// knobs), thumb indicator on the cap, optional tick ring that lights up
// between anchor and value.
//
// Interaction:
//   * Vertical drag (default): drag up = increase, ~150 px = full travel.
//   * Circular: pointer angle around the center maps onto the arc.
//   * Revolution: one full 360° turn of accumulated angular travel =
//     full range.
//   Shift during any drag = 4x slower, applied per-move so only the
//   stretch held with Shift is slowed. Wheel steps by step only while
//   focused. Arrows = step, Shift+arrows = fineStep, PageUp/Down =
//   largeStep, Home/End = min/max. Double-click, Alt+click or
//   Delete/Backspace resets to SetResetValue.
// Callback split: OnValueChange live, OnValueCommitted once per gesture
// (drag end / wheel-key idle / reset) — never for programmatic SetValue.
class Knob final : public Widget {
public:
    enum class DragMode { Vertical, Circular, Revolution };
    enum class Scale { Linear, Log };

    static std::shared_ptr<Knob> Create();

    // ---- value ----
    // Value is stored in real units; default range is normalized 0..1.
    void SetValue(float value);
    float GetValue() const { return value_; }
    float Value() const { return value_; }
    void SetNormalizedValue(float t);
    float GetNormalizedValue() const;
    void SetRange(float minimum, float maximum);
    float GetMin() const { return minimum_; }
    float GetMax() const { return maximum_; }

    // ---- scale ----
    void SetScale(Scale scale);
    Scale GetScale() const { return scale_; }
    // Custom taper pair (normalized position <-> real value). Overrides
    // SetScale until ClearTaper().
    void SetTaper(std::function<float(float)> normalized_to_value,
                  std::function<float(float)> value_to_normalized);
    void ClearTaper();

    // ---- drag ----
    void SetDragMode(DragMode mode);
    DragMode GetDragMode() const { return drag_mode_; }
    // Vertical drag: pixels of travel for the full range (default 150).
    void SetVerticalSensitivity(float px_per_range);
    // Where the indicator arc grows from, in real units — 0 on a -1..1
    // pan knob. Clamped into the range. Default: range minimum.
    void SetAnchor(float value);
    float GetAnchor() const { return anchor_; }

    // ---- detents ----
    // Snap points in real units. While a gesture passes within the pull
    // zone the value snaps to the detent.
    void SetDetents(std::vector<float> values);
    const std::vector<float>& GetDetents() const { return detents_; }
    // Pull strength 0..1 scales the snap zone (1 = 4% of the range on
    // each side of a detent). Default 1.
    void SetDetentPull(float strength);
    float GetDetentPull() const { return detent_pull_; }

    // ---- steps (normalized fractions of the range) ----
    // Arrow keys / wheel notch = step (0.01), Shift = fineStep (0.0025),
    // PageUp/Down = largeStep (0.1).
    void SetStep(float normalized_step);
    void SetFineStep(float normalized_step);
    void SetLargeStep(float normalized_step);

    // ---- reset ----
    // Double-click, Alt+click or Delete/Backspace restores this value.
    // Unset by default.
    void SetResetValue(float value);
    void ClearResetValue();
    bool HasResetValue() const { return reset_value_.has_value(); }

    // ---- display ----
    void SetLabel(std::string text);
    const std::string& GetLabel() const { return label_; }
    // Unit suffix appended to the value readout ("dB", "%", "Hz").
    void SetUnit(std::string suffix);
    const std::string& GetUnit() const { return unit_; }
    // Tick ring around the arc (default on).
    void SetShowTicks(bool show);
    bool GetShowTicks() const { return show_ticks_; }
    // Readout override; receives the real value, returns the complete
    // string (unit is NOT auto-appended).
    void SetValueFormatter(std::function<std::string(float)> formatter);
    std::string FormatValue(float value) const;

    // ---- callbacks ----
    void SetOnValueChange(std::function<void(float)> callback);
    void SetOnValueCommitted(std::function<void(float)> callback);

    bool IsDragging() const { return dragging_; }

    void Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) override;
    void Layout(const Layout::Rect& bounds) override;
    using Widget::Layout;
    void Tick(double dt_seconds) override;
    bool HandleEvent(const Event& event) override;

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

    std::string GetDebugText() const override;

private:
    Knob();

    float NormalizedToValue(float t) const;
    float ValueToNormalized(float value) const;
    // Gesture path: detent snap, clamp, store, live callback, debounced
    // commit arm.
    void ApplyGestureNormalized(float t);
    void CommitGesture();
    void Reset();
    // Angle helpers — clockwise degrees from 12 o'clock; the arc runs
    // 225deg (bottom-left) through 0 to 135deg+360 (bottom-right).
    static float AngleForT(float t);
    static float TForPointerAngle(float degrees);
    float PointerAngle(const Layout::Point& position) const;
    Layout::Rect KnobCircle() const;
    void StrokeArc(Rendering::Renderer& renderer, float cx, float cy,
                   float radius, float a0, float a1, Theme::Color color,
                   float width) const;

    float value_ = 0.0f;
    float minimum_ = 0.0f;
    float maximum_ = 1.0f;
    Scale scale_ = Scale::Linear;
    std::function<float(float)> taper_to_value_;
    std::function<float(float)> taper_to_normalized_;
    DragMode drag_mode_ = DragMode::Vertical;
    float vertical_sensitivity_ = 150.0f;
    float anchor_ = 0.0f;
    std::vector<float> detents_;
    float detent_pull_ = 1.0f;
    float step_ = 0.01f;
    float fine_step_ = 0.0025f;
    float large_step_ = 0.1f;
    std::optional<float> reset_value_;
    std::string label_;
    std::string unit_;
    bool show_ticks_ = true;
    std::function<std::string(float)> value_formatter_;
    std::function<void(float)> on_value_change_;
    std::function<void(float)> on_value_committed_;

    // --- drag state ---
    bool dragging_ = false;
    bool just_reset_ = false;    // Alt+click shouldn't also start a drag
    float last_y_ = 0.0f;        // Vertical
    float last_angle_ = 0.0f;    // Circular/Revolution (raw degrees)

    // --- commit debounce ---
    // commit_idle_ accumulates Tick() seconds since the last nudge — tick
    // time, not wall clock, so tests drive it deterministically.
    bool pending_commit_ = false;
    double commit_idle_ = 0.0;

    // --- double-click ---
    std::chrono::steady_clock::time_point last_click_{};
    Layout::Point last_click_pos_{};

    Motion::Tween hover_glow_;
    bool was_focused_ = false;
};

} // namespace XER::UI::Widgets

#endif // XER_UI_WIDGETS_KNOB_H
