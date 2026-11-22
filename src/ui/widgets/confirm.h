#ifndef XER_UI_WIDGETS_CONFIRM_H
#define XER_UI_WIDGETS_CONFIRM_H

#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include "motion/motion.h"
#include "widgets/button.h"
#include "widgets/widget.h"

namespace XER::UI::Widgets {

// SureUI-style risk-matched confirmation gestures (sureui.com
// confirm-button): one SetOnConfirm that fires exactly once after the
// mode's gesture completes.
enum class ConfirmMode {
    // Medium risk: first activation arms the control (label swaps to
    // the armed text, warning-tinted chrome); a second activation
    // within the arm window confirms. The arm lapses after
    // SetArmTimeout seconds (default 3).
    ClickAgain,
    // High risk: press-and-hold fills the control until
    // SetHoldDuration seconds (default 0.8) elapse, then fires.
    // Releasing early resets the progress. Keyboard activation
    // (Enter/Space) can't hold, so it behaves like ClickAgain: first
    // press arms, second confirms.
    HoldToConfirm,
    // Critical risk: activation arms an inline phrase field in place
    // of the label; typing the exact SetConfirmPhrase confirms
    // (fires on match). Escape / losing focus disarms.
    TypePhrase
};

// A button-chrome control hosting the ConfirmMode gestures above.
// Self-contained (Button is final): it reuses ButtonStyle/ButtonSize
// and the same visual tokens, adding arm/hold/phrase states.
class ConfirmButton final : public Widget {
public:
    explicit ConfirmButton(
        std::string text = {},
        ButtonStyle style = ButtonStyle::Secondary);

    void SetText(std::string text);
    const std::string& GetText() const { return text_; }
    std::string GetDebugText() const override { return text_; }
    // Fires exactly once when the confirm gesture completes. The
    // button auto-disarms first, so the callback sees IsArmed()==false.
    void SetOnConfirm(std::function<void()> callback);
    void SetStyle(ButtonStyle style);
    void SetSize(ButtonSize size);
    ButtonSize GetSize() const { return size_; }

    void SetConfirmMode(ConfirmMode mode);
    ConfirmMode GetConfirmMode() const { return mode_; }
    // True while armed (ClickAgain window open, TypePhrase field
    // showing). HoldToConfirm's in-flight press is HoldProgress()>0.
    bool IsArmed() const { return armed_; }
    // Returns to idle without firing (also used programmatically).
    void Disarm();

    // ClickAgain: label shown while armed (default "Confirm?").
    void SetArmedText(std::string text);
    // ClickAgain: seconds before an arm lapses back to idle.
    void SetArmTimeout(double seconds);
    // HoldToConfirm: seconds the press must be held before firing.
    void SetHoldDuration(double seconds);
    // 0..1 progress of the current hold (0 when not holding).
    float HoldProgress() const;

    // TypePhrase: exact string the user must type to confirm (e.g.
    // "DELETE"). An empty phrase arms but can never match — set one.
    void SetConfirmPhrase(std::string phrase);
    const std::string& GetConfirmPhrase() const { return phrase_; }
    // Current contents of the armed phrase field (TypePhrase).
    const std::string& GetTyped() const { return typed_; }

    void Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) override;
    void Layout(const Layout::Rect& bounds) override;
    using Widget::Layout;
    void Tick(double dt_seconds) override;
    bool HandleEvent(const Event& event) override;

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

private:
    void Activate();       // one activation step (click / Enter / Space)
    // Disarm, then run on_confirm_ once. swallow_trailing_click is only
    // for the hold-completion path: the press is still down there, so
    // the release's synthesized Click must be eaten. Click/key-driven
    // fires pass false — the next real click must re-arm normally.
    void Fire(bool swallow_trailing_click);
    float HeightForSize() const;
    std::size_t Utf8Length(std::string_view text) const;

    std::string text_;
    std::string armed_text_ = "Confirm?";
    std::string phrase_;
    std::string typed_;    // TypePhrase draft while armed
    std::function<void()> on_confirm_;
    ButtonStyle style_;
    ButtonSize size_ = ButtonSize::Default;
    ConfirmMode mode_ = ConfirmMode::ClickAgain;
    bool armed_ = false;
    double arm_elapsed_ = 0.0;
    double arm_timeout_ = 3.0;
    bool holding_ = false; // HoldToConfirm press in flight
    double hold_elapsed_ = 0.0;
    double hold_duration_ = 0.8;
    bool held_ = false;    // pointer currently pressed (press depth)
    // Swallow the synthesized click that follows a completed hold (or
    // a keyboard-fired confirm) so one gesture can't arm twice.
    bool suppress_click_pending_ = false;
    bool was_focused_ = false; // focus-loss disarm detection

    Motion::Spring press_;
    Motion::Tween hover_t_;
    // Armed-state fade-in of the warning/phrase chrome.
    Motion::Tween arm_t_;
};

} // namespace XER::UI::Widgets

#endif // XER_UI_WIDGETS_CONFIRM_H
