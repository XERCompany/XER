#ifndef XER_UI_WIDGETS_SCRUB_NUMBER_FIELD_H
#define XER_UI_WIDGETS_SCRUB_NUMBER_FIELD_H

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "motion/motion.h"
#include "widgets/widget.h"

namespace XER::UI::Widgets {

// ScrubNumberField — Figma/NLE drag-scrub numeric input (kinetic-style).
// Two gestures share one field:
//   * click = text edit (shows the full value, select-all on entry; Enter
//     commits, Escape reverts, blur commits),
//   * press-and-drag horizontally = scrub the value (the tree's mouse
//     capture gives the "cursor-locked" endless drag past the field edge).
// Modifiers resolve the step: Alt = smallStep, Shift = largeStep.
// Wheel nudges by step only while focused (opt-in via SetAllowWheelScrub),
// double-click resets to SetResetValue, Home/End jump to min/max.
// Callback split follows the kit convention: OnValueChange fires live on
// every change, OnValueCommitted fires once per gesture (drag end, Enter,
// blur, reset) — never for programmatic SetValue.
class ScrubNumberField final : public Widget {
public:
    // Edge feedback shown when the value pins at min/max.
    enum class BoundFeedback {
        None,
        Shade,          // persistent end-cap shade while sitting at a bound
        Flash,          // transient flash when a gesture hits a bound
        ShadeAndFlash   // both
    };

    static std::shared_ptr<ScrubNumberField> Create();

    // ---- value ----
    void SetValue(float value);
    float GetValue() const { return value_; }
    float Value() const { return value_; }
    void SetMin(float minimum);
    void SetMax(float maximum);
    void SetRange(float minimum, float maximum);
    float GetMin() const { return minimum_; }
    float GetMax() const { return maximum_; }

    // ---- steps ----
    // Default step is 1; Alt uses smallStep (default 0.1), Shift uses
    // largeStep (default 10).
    void SetStep(float step);
    void SetSmallStep(float step);
    void SetLargeStep(float step);

    // ---- scrub ----
    // Pixels of horizontal travel per step while dragging (default 2).
    void SetPixelSensitivity(float px_per_step);
    // Opt-in wheel scrubbing; only ever acts while the field is focused.
    void SetAllowWheelScrub(bool allow);
    bool GetAllowWheelScrub() const { return allow_wheel_scrub_; }
    // Double-click (and Delete/Backspace while focused) restores this
    // value. Unset by default — reset is disabled until one is provided.
    void SetResetValue(float value);
    void ClearResetValue();
    bool HasResetValue() const { return reset_value_.has_value(); }
    void SetBoundFeedback(BoundFeedback feedback);
    BoundFeedback GetBoundFeedback() const { return bound_feedback_; }

    // ---- display ----
    // printf-style pattern for the numeric part (default "%.4g").
    void SetFormat(std::string printf_pattern);
    // Full display override; wins over SetFormat. Receives the value,
    // returns the complete string (unit is NOT auto-appended).
    void SetValueFormatter(std::function<std::string(float)> formatter);
    // Suffix drawn muted after the value ("dB", "%", "px"); stripped
    // before parsing edited text.
    void SetUnit(std::string suffix);
    const std::string& GetUnit() const { return unit_; }
    std::string FormatValue(float value) const;

    // ---- callbacks ----
    // Live — every change including mid-drag.
    void SetOnValueChange(std::function<void(float)> callback);
    // Committed — once per gesture: scrub end, Enter, blur, wheel/key
    // idle, reset.
    void SetOnValueCommitted(std::function<void(float)> callback);

    bool IsEditing() const { return editing_; }
    bool IsScrubbing() const { return scrubbing_; }
    // Enters text-edit mode with the whole value selected (same as a
    // plain click).
    void BeginEdit();

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
    ScrubNumberField();

    // Resolved step for the current modifiers: Alt → small, Shift → large.
    float ResolveStep(bool alt, bool shift) const;
    // Gesture path: clamps, stores, fires live callback, arms the
    // debounced commit, and pulses bound feedback when pinning a bound.
    void ApplyGestureValue(float value);
    void CommitGesture();
    void TriggerBoundFlash(float side);
    bool ParseEditText(float* value_out) const;
    void CommitEdit();
    void CancelEdit();

    // --- inline text-edit helpers (single-line numeric editor) ---
    bool HandleEditKey(const Event& event);
    void InsertEditText(const std::string& text);
    bool HasEditSelection() const;
    std::pair<std::size_t, std::size_t> EditSelectionRange() const;
    void EraseEditRange(std::size_t begin, std::size_t end);
    void MoveEditCaret(std::size_t position, bool extend_selection);
    std::size_t EditCaretIndexForX(
        float x,
        Rendering::Renderer& renderer,
        const Layout::Rect& text_bounds,
        const Rendering::TextStyle& style) const;

    float value_ = 0.0f;
    float minimum_ = -1.0e30f;
    float maximum_ = 1.0e30f;
    float step_ = 1.0f;
    float small_step_ = 0.1f;
    float large_step_ = 10.0f;
    float pixel_sensitivity_ = 2.0f;
    bool allow_wheel_scrub_ = true;
    std::optional<float> reset_value_;
    BoundFeedback bound_feedback_ = BoundFeedback::ShadeAndFlash;
    std::string format_ = "%.4g";
    std::function<std::string(float)> value_formatter_;
    std::string unit_;
    std::function<void(float)> on_value_change_;
    std::function<void(float)> on_value_committed_;

    // --- drag state ---
    bool pressed_ = false;
    bool scrubbing_ = false;
    // Click suppression: a scrub that ends inside the bounds must not be
    // read as a click-to-edit.
    bool just_scrubbed_ = false;
    float press_x_ = 0.0f;
    // Anchor (accumulated px, value) the current step-kind started from;
    // re-anchored when Alt/Shift changes mid-drag so modifier flips never
    // jump the value.
    float anchor_dx_ = 0.0f;
    float anchor_value_ = 0.0f;
    bool anchor_alt_ = false;
    bool anchor_shift_ = false;

    // --- edit state ---
    bool editing_ = false;
    std::string edit_text_;
    std::size_t caret_ = 0;
    std::size_t selection_anchor_ = 0;
    std::optional<float> pending_caret_x_;

    // --- commit debounce (wheel + keyboard nudges) ---
    // commit_idle_ accumulates Tick() seconds since the last nudge — tick
    // time, not wall clock, so tests drive it deterministically.
    bool pending_commit_ = false;
    double commit_idle_ = 0.0;

    // --- bound flash ---
    Motion::Tween bound_flash_;
    float bound_side_ = 0.0f;   // -1 min, +1 max

    // --- double-click ---
    std::chrono::steady_clock::time_point last_click_{};
    Layout::Point last_click_pos_{};
    bool was_focused_ = false;
};

} // namespace XER::UI::Widgets

#endif // XER_UI_WIDGETS_SCRUB_NUMBER_FIELD_H
