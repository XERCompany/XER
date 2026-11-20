#include "speed_dialog.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>
#include <utility>

#include "rendering/renderer.h"
#include "theme/tokens.h"
#include "widgets/button.h"
#include "widgets/checkbox.h"
#include "widgets/label.h"
#include "widgets/text_input.h"

namespace XER::VideoEditor::Ui {
namespace {

namespace Layout = XER::UI::Layout;
namespace Rendering = XER::UI::Rendering;

// TimelineModel::SetClipRate accepts any positive rate; the dialog keeps
// the Premiere-like useful band (1%..10000% == 0.01x..100x).
constexpr double kMinRate = 0.01;
constexpr double kMaxRate = 100.0;

constexpr float kPanelWidth = 480.0f;
constexpr float kPanelHeight = 350.0f;
constexpr float kPanelPad = 22.0f;

// Lenient numeric parse: empty/garbage/non-finite -> NaN (invalid).
double ParseNumber(const std::string& text) {
    if (text.empty()) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    char* end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    if (end == text.c_str() || !std::isfinite(value)) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return value;
}

std::string FormatSeconds(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0.0) seconds = 0.0;
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.3f", seconds);
    return buffer;
}

std::string FormatSpeed(double percent) {
    if (!std::isfinite(percent)) percent = 0.0;
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.2f", percent);
    return buffer;
}

}  // namespace

SpeedDialog::SpeedDialog(Hooks hooks) : hooks_(std::move(hooks)) {
    SetFocusable(true);
    SetClipsChildren(false);
}

void SpeedDialog::EnsureChildren() {
    if (children_attached_) return;
    children_attached_ = true;
    namespace Widgets = XER::UI::Widgets;
    title_ = std::make_shared<Widgets::Label>("Clip speed / duration");
    info_label_ = std::make_shared<Widgets::Label>("", true);
    speed_input_ = std::make_shared<Widgets::TextInput>("100.00");
    duration_input_ = std::make_shared<Widgets::TextInput>("0.000");
    readout_label_ = std::make_shared<Widgets::Label>("", true);
    reverse_check_ = std::make_shared<Widgets::Checkbox>(false);
    ripple_check_ = std::make_shared<Widgets::Checkbox>(false);

    speed_input_->SetOnValueChanged([this](const std::string&) {
        if (syncing_fields_) return;
        last_edit_ = LastEdit::kSpeed;
        // Link fields: the consumed source span stays constant, so a
        // speed edit re-times the clip — duration = consumed / rate.
        const double speed = ParsedSpeedPercent();
        if (std::isfinite(speed) && speed > 0.0) {
            const double rate = speed / 100.0;
            if (rate > 0.0 && consumed_ > 0.0) {
                syncing_fields_ = true;
                duration_input_->SetValue(
                    FormatSeconds(consumed_ / rate));
                syncing_fields_ = false;
            }
        }
        RefreshValidation();
    });
    duration_input_->SetOnValueChanged([this](const std::string&) {
        if (syncing_fields_) return;
        last_edit_ = LastEdit::kDuration;
        // Inverse link: a duration edit re-derives the rate that keeps
        // the same source window — rate = consumed / new_duration.
        const double duration = ParsedDurationSeconds();
        if (std::isfinite(duration) && duration > 0.0 &&
            consumed_ > 0.0) {
            const double rate = consumed_ / duration;
            if (std::isfinite(rate) && rate > 0.0) {
                syncing_fields_ = true;
                speed_input_->SetValue(FormatSpeed(rate * 100.0));
                syncing_fields_ = false;
            }
        }
        RefreshValidation();
    });
    // Enter inside either numeric field applies the dialog.
    const auto submit = [this] { Apply(); };
    speed_input_->SetOnSubmit(submit);
    duration_input_->SetOnSubmit(submit);

    apply_button_ = std::make_shared<Widgets::Button>(
        "Apply", Widgets::ButtonStyle::Primary);
    apply_button_->SetOnClick([this] { Apply(); });
    cancel_button_ = std::make_shared<Widgets::Button>(
        "Cancel", Widgets::ButtonStyle::Secondary);
    cancel_button_->SetOnClick([this] { Cancel(); });

    AddChild(title_);
    AddChild(info_label_);
    AddChild(speed_input_);
    AddChild(duration_input_);
    AddChild(readout_label_);
    AddChild(reverse_check_);
    AddChild(ripple_check_);
    AddChild(apply_button_);
    AddChild(cancel_button_);
}

void SpeedDialog::SetClip(double in_point, double duration, double rate,
                          double media_duration) {
    EnsureChildren();
    in_point_ = std::isfinite(in_point) ? std::max(0.0, in_point) : 0.0;
    media_duration_ = media_duration;   // <= 0: unbounded
    if (!std::isfinite(rate) || rate <= 0.0) rate = 1.0;
    if (!std::isfinite(duration) || duration <= 0.0) duration = 0.001;
    consumed_ = duration * rate;
    fallback_rate_ = rate;
    last_edit_ = LastEdit::kSpeed;
    syncing_fields_ = true;
    speed_input_->SetValue(FormatSpeed(rate * 100.0));
    duration_input_->SetValue(FormatSeconds(duration));
    syncing_fields_ = false;
    reverse_check_->SetChecked(false);
    ripple_check_->SetChecked(false);
    char info[160];
    if (media_duration_ > 0.0) {
        std::snprintf(info, sizeof(info),
                      "In %.3f s \u00b7 source %.3f s", in_point_,
                      media_duration_);
    } else {
        std::snprintf(info, sizeof(info),
                      "In %.3f s \u00b7 source unbounded", in_point_);
    }
    info_label_->SetText(info);
    RefreshValidation();
    Invalidate();
}

double SpeedDialog::ParsedSpeedPercent() const {
    return speed_input_ ? ParseNumber(speed_input_->GetValue())
                        : std::numeric_limits<double>::quiet_NaN();
}

double SpeedDialog::ParsedDurationSeconds() const {
    return duration_input_ ? ParseNumber(duration_input_->GetValue())
                           : std::numeric_limits<double>::quiet_NaN();
}

double SpeedDialog::GetRate() const {
    double rate = fallback_rate_;
    if (last_edit_ == LastEdit::kDuration && consumed_ > 0.0) {
        const double duration = ParsedDurationSeconds();
        if (std::isfinite(duration) && duration > 0.0) {
            rate = consumed_ / duration;
        }
    } else {
        const double speed = ParsedSpeedPercent();
        if (std::isfinite(speed)) {
            rate = speed / 100.0;
        }
    }
    if (!std::isfinite(rate) || rate <= 0.0) rate = fallback_rate_;
    return std::clamp(rate, kMinRate, kMaxRate);
}

bool SpeedDialog::Ripple() const {
    return ripple_check_ != nullptr && ripple_check_->IsChecked();
}

bool SpeedDialog::Reverse() const {
    return reverse_check_ != nullptr && reverse_check_->IsChecked();
}

bool SpeedDialog::Valid() const {
    const double speed = ParsedSpeedPercent();
    const double duration = ParsedDurationSeconds();
    if (!std::isfinite(speed) || speed <= 0.0) return false;
    if (!std::isfinite(duration) || duration <= 0.0) return false;
    const double rate = last_edit_ == LastEdit::kDuration && consumed_ > 0.0
                            ? consumed_ / duration
                            : speed / 100.0;
    if (!std::isfinite(rate) || rate < kMinRate || rate > kMaxRate) {
        return false;
    }
    // An already-overflowing clip (in_point + consumed > media) is NOT
    // rejected: the consumed span is preserved by every field edit, so
    // no user input could ever clear the flag — disabling Apply would
    // leave the dialog permanently stuck. The apply path clamps the
    // source window to the media bound instead (see ApplyClipSpeed).
    return true;
}

std::string SpeedDialog::HintText() const {
    const double speed = ParsedSpeedPercent();
    const double duration = ParsedDurationSeconds();
    if (!std::isfinite(speed)) {
        return "Speed must be a number (1\u201310000 %).";
    }
    if (speed <= 0.0 || speed / 100.0 < kMinRate ||
        speed / 100.0 > kMaxRate) {
        return "Speed must be 1\u201310000 % (0.01x\u2013100x).";
    }
    if (!std::isfinite(duration)) {
        return "Duration must be a number of seconds.";
    }
    if (duration <= 0.0) {
        return "Duration must be greater than zero.";
    }
    return {};
}

void SpeedDialog::RefreshValidation() {
    if (!children_attached_) return;
    const bool valid = Valid();
    hint_text_ = HintText();
    apply_button_->SetEnabled(valid);

    // Live readout: consumed source span is invariant; show it plus the
    // rate/new-duration pair the apply would produce.
    if (valid) {
        const double rate = GetRate();
        const double duration = ParsedDurationSeconds();
        // A clip that already overflows its media gets clamped on
        // apply — say so in the readout.
        const bool overflows =
            media_duration_ > 0.0 &&
            in_point_ + consumed_ > media_duration_ + 1e-6;
        char buffer[224];
        std::snprintf(buffer, sizeof(buffer),
                      "Consumes %.3f s of source (%.3f\u2013%.3f) \u00b7 "
                      "rate %.2fx \u00b7 new duration %.3f s%s",
                      consumed_, in_point_, in_point_ + consumed_, rate,
                      duration,
                      overflows ? " \u2014 trims to media end" : "");
        readout_label_->SetText(buffer);
    } else {
        char buffer[128];
        std::snprintf(buffer, sizeof(buffer),
                      "Consumes %.3f s of source (%.3f\u2013%.3f)",
                      consumed_, in_point_, in_point_ + consumed_);
        readout_label_->SetText(buffer);
    }
    Invalidate();
}

void SpeedDialog::Apply() {
    if (!Valid() || !hooks_.on_apply) return;
    hooks_.on_apply(GetRate(), Ripple(), Reverse());
}

void SpeedDialog::Cancel() {
    if (hooks_.on_cancel) hooks_.on_cancel();
}

Layout::Rect SpeedDialog::PanelRect() const {
    const auto bounds = GetBounds();
    const float width = std::min(kPanelWidth, bounds.width - 40.0f);
    return {bounds.x + (bounds.width - width) * 0.5f,
            bounds.y + (bounds.height - kPanelHeight) * 0.5f,
            width, kPanelHeight};
}

Layout::Rect SpeedDialog::ReverseRowRect() const {
    const auto panel = PanelRect();
    return {panel.x + kPanelPad, panel.y + 212.0f,
            panel.width - kPanelPad * 2.0f, 24.0f};
}

Layout::Rect SpeedDialog::RippleRowRect() const {
    const auto panel = PanelRect();
    return {panel.x + kPanelPad, panel.y + 240.0f,
            panel.width - kPanelPad * 2.0f, 24.0f};
}

void SpeedDialog::Render(
    Rendering::Renderer& renderer,
    const XER::UI::Theme::Tokens& tokens) {
    EnsureChildren();
    const auto bounds = GetBounds();
    renderer.FillRect(bounds, {0.0f, 0.0f, 0.0f, 0.58f}, 0.0f);
    const auto panel = PanelRect();
    renderer.FillRect(panel, tokens.surface, tokens.radius_large);
    renderer.StrokeRect(panel, tokens.border, 1.0f, tokens.radius_large);
    renderer.FillRect({panel.x, panel.y, 4.0f, panel.height},
                      tokens.accent, tokens.radius_large);

    // Field captions.
    const float px = panel.x + kPanelPad;
    const Rendering::TextStyle caption{tokens.muted, tokens.font_small,
                                       600};
    renderer.DrawText("Speed (%)",
                      {px, panel.y + 88.0f, 150.0f, 14.0f}, caption);
    renderer.DrawText("Duration (s)",
                      {px + 180.0f, panel.y + 88.0f, 150.0f, 14.0f},
                      caption);

    // Checkbox captions (the box itself is a Checkbox child; the text
    // is drawn here so the whole row stays clickable by the dialog).
    const Rendering::TextStyle check_text{
        tokens.foreground, tokens.font_body, 500};
    const Rendering::TextStyle check_note{tokens.muted, tokens.font_small,
                                          400};
    const auto reverse_row = ReverseRowRect();
    renderer.DrawText("Reverse",
                      {reverse_row.x + 26.0f, reverse_row.y + 2.0f,
                       90.0f, 20.0f},
                      check_text);
    renderer.DrawText("(preview \u2014 not applied yet)",
                      {reverse_row.x + 106.0f, reverse_row.y + 4.0f,
                       220.0f, 16.0f},
                      check_note);
    const auto ripple_row = RippleRowRect();
    renderer.DrawText("Ripple \u2014 shift trailing clips",
                      {ripple_row.x + 26.0f, ripple_row.y + 2.0f,
                       260.0f, 20.0f},
                      check_text);

    // Validation hint in danger red (not a child — Label has no
    // danger-colored variant).
    if (!hint_text_.empty()) {
        renderer.DrawText(hint_text_,
                          {px, panel.y + 178.0f,
                           panel.width - kPanelPad * 2.0f, 18.0f},
                          {tokens.danger, tokens.font_small, 500});
    }
}

void SpeedDialog::Layout(const XER::UI::Layout::Rect& bounds) {
    EnsureChildren();
    SetBounds(bounds);
    const auto& tokens = CachedLayoutTokens();
    const auto panel = PanelRect();
    const float px = panel.x + kPanelPad;
    const float pw = panel.width - kPanelPad * 2.0f;

    title_->Layout({px, panel.y + 14.0f, pw, 26.0f}, tokens);
    info_label_->Layout({px, panel.y + 42.0f, pw, 18.0f}, tokens);

    const float field_y = panel.y + 108.0f;
    speed_input_->Layout({px, field_y, 150.0f, 32.0f}, tokens);
    duration_input_->Layout({px + 180.0f, field_y, 150.0f, 32.0f},
                            tokens);

    readout_label_->Layout({px, panel.y + 156.0f, pw, 18.0f}, tokens);

    const auto reverse_row = ReverseRowRect();
    reverse_check_->Layout({reverse_row.x, reverse_row.y + 2.0f, 20.0f,
                            20.0f},
                           tokens);
    const auto ripple_row = RippleRowRect();
    ripple_check_->Layout({ripple_row.x, ripple_row.y + 2.0f, 20.0f,
                           20.0f},
                          tokens);

    const float button_y = panel.y + panel.height - 48.0f;
    cancel_button_->Layout({panel.x + panel.width - kPanelPad - 196.0f,
                            button_y, 94.0f, 34.0f},
                           tokens);
    apply_button_->Layout({panel.x + panel.width - kPanelPad - 94.0f,
                           button_y, 94.0f, 34.0f},
                          tokens);
}

bool SpeedDialog::HandleEvent(const XER::UI::Widgets::Event& event) {
    using XER::UI::Widgets::Event;
    using XER::UI::Widgets::MouseButton;
    if (event.type == Event::Type::KeyDown) {
        if (event.key == XER::UI::Widgets::KeyCode::Escape) {
            Cancel();
            return true;
        }
        if (event.key == XER::UI::Widgets::KeyCode::Enter) {
            Apply();
            return true;
        }
    }
    if (event.type == Event::Type::Click &&
        event.mouse_button == MouseButton::Left) {
        // Whole-row hit targets: the caption text next to each box.
        if (ReverseRowRect().Contains(event.position)) {
            reverse_check_->SetChecked(!reverse_check_->IsChecked());
            return true;
        }
        if (RippleRowRect().Contains(event.position)) {
            ripple_check_->SetChecked(!ripple_check_->IsChecked());
            return true;
        }
        // Backdrop click outside the card cancels (modal convention).
        if (!PanelRect().Contains(event.position)) {
            Cancel();
            return true;
        }
    }
    // Modal: consume pointer events inside and outside the panel so the
    // workspace below stays inert while the dialog is up.
    return event.type == Event::Type::MouseDown ||
           event.type == Event::Type::MouseUp ||
           event.type == Event::Type::Click;
}

void SpeedDialog::Tick(double dt_seconds) { (void)dt_seconds; }

}  // namespace XER::VideoEditor::Ui
