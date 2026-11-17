// Modal "Clip Speed / Duration" overlay for the XER video editor —
// Premiere's Ctrl+R dialog. Follows the SettingsDialog/ExportDialog
// pattern: dimmed backdrop, centered card, Escape/backdrop cancel,
// Enter applies.
//
// The dialog owns no project state: the shell pushes the clip's
// current values through SetClip() before showing it and reads the
// result out of the on_apply hook (rate, ripple, reverse). The
// consumed SOURCE span (duration * rate) is held constant while the
// user edits either field — editing speed re-times the clip to a new
// duration, editing duration re-derives the rate — matching
// TimelineModel::SetClipRate semantics (in_point and the consumed
// source window stay put; the clip shrinks or grows on the timeline).
#pragma once

#include <functional>
#include <memory>
#include <string>

#include "layout/geometry.h"
#include "widgets/widget.h"

namespace XER::UI::Widgets {
class Button;
class Checkbox;
class Label;
class TextInput;
}

namespace XER::VideoEditor::Ui {

class SpeedDialog final : public XER::UI::Widgets::Widget {
public:
    struct Hooks {
        // Fired by Apply with the validated rate (always inside
        // [0.01, 100]), the ripple flag (host shifts trailing clips)
        // and the reverse flag (display-only for now — the model has
        // no reverse playback yet; the host may ignore it).
        std::function<void(double rate, bool ripple, bool reverse)>
            on_apply;
        // Fired by Cancel / Escape / backdrop click. Optional — the
        // host usually just hides the overlay.
        std::function<void()> on_cancel;
    };

    explicit SpeedDialog(Hooks hooks = {});

    // Fills the fields from the clip being re-timed. `rate` is the
    // current playback_rate; `media_duration` <= 0 means the source is
    // unbounded (generated clips, unknown bounds). Call before showing.
    void SetClip(double in_point, double duration, double rate,
                 double media_duration);

    // Validated rate per the last-edited field (speed% / 100, or
    // consumed/new_duration), clamped to [0.01, 100]. Returns the
    // clip's original rate while the fields are invalid. Never throws.
    double GetRate() const;
    bool Ripple() const;
    bool Reverse() const;
    // True while every field parses, the derived rate lands inside
    // [0.01, 100] and the consumed span fits the media bounds (when
    // known). Invalid fields show a red hint and disable Apply.
    bool Valid() const;

    void Render(XER::UI::Rendering::Renderer& renderer,
                const XER::UI::Theme::Tokens& tokens) override;
    void Layout(const XER::UI::Layout::Rect& bounds) override;
    using Widget::Layout;
    bool HandleEvent(const XER::UI::Widgets::Event& event) override;
    void Tick(double dt_seconds) override;

    XER::UI::Layout::Size GetMinSize() const override {
        return {100000.0f, 100000.0f};
    }
    XER::UI::Layout::Size GetPreferredSize() const override {
        return {100000.0f, 100000.0f};
    }
    XER::UI::Layout::Size GetMaxSize() const override {
        return {100000.0f, 100000.0f};
    }

private:
    void EnsureChildren();
    void Apply();
    void Cancel();
    // Recomputes the readout line, the red hint and Apply enablement
    // from the current field contents.
    void RefreshValidation();
    // NaN when the field does not parse to a finite number.
    double ParsedSpeedPercent() const;
    double ParsedDurationSeconds() const;
    std::string HintText() const;

    XER::UI::Layout::Rect PanelRect() const;
    // Clickable rows for the two checkboxes (the box child covers only
    // its own square; the caption drawn next to it is part of the row).
    XER::UI::Layout::Rect ReverseRowRect() const;
    XER::UI::Layout::Rect RippleRowRect() const;

    Hooks hooks_;
    bool children_attached_ = false;
    // Held while a field callback rewrites the linked field so the
    // re-entrant on_value_changed does not bounce the edit back.
    bool syncing_fields_ = false;
    double in_point_ = 0.0;
    double media_duration_ = 0.0;   // <= 0: unbounded
    // Source span consumed at SetClip() time — the quantity both edit
    // paths preserve.
    double consumed_ = 0.0;
    // The clip's rate on entry; GetRate() falls back to it while the
    // fields are invalid.
    double fallback_rate_ = 1.0;
    // Which numeric field the user last touched — GetRate() uses its
    // formula so duration-driven edits keep full double precision.
    enum class LastEdit { kSpeed, kDuration };
    LastEdit last_edit_ = LastEdit::kSpeed;

    std::shared_ptr<XER::UI::Widgets::Label> title_;
    std::shared_ptr<XER::UI::Widgets::Label> info_label_;
    std::shared_ptr<XER::UI::Widgets::TextInput> speed_input_;
    std::shared_ptr<XER::UI::Widgets::TextInput> duration_input_;
    std::shared_ptr<XER::UI::Widgets::Label> readout_label_;
    // Current validation message — drawn in red by Render() (Label has
    // no danger color, so the hint is not a child widget).
    std::string hint_text_;
    std::shared_ptr<XER::UI::Widgets::Checkbox> reverse_check_;
    std::shared_ptr<XER::UI::Widgets::Checkbox> ripple_check_;
    std::shared_ptr<XER::UI::Widgets::Button> apply_button_;
    std::shared_ptr<XER::UI::Widgets::Button> cancel_button_;
};

}  // namespace XER::VideoEditor::Ui
