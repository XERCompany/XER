#include "scrub_number_field.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <system_error>
#include <utility>

#include "content/clipboard/clipboard.h"
#include "rendering/renderer.h"
#include "theme/tokens.h"

namespace XER::UI::Widgets {
namespace {

// Horizontal travel needed before a press becomes a scrub (below this it
// stays a click-to-edit).
constexpr float kDragThreshold = 3.0f;
constexpr auto kDoubleClickWindow = std::chrono::milliseconds(450);
constexpr float kDoubleClickRadius = 12.0f;
// Wheel/keyboard nudges coalesce into one committed value after this much
// idle tick-time, matching "committed = gesture end" semantics.
constexpr double kCommitDebounceSeconds = 0.4;
// Values clamped to +/-1e30 are treated as "no real bound" — bound
// feedback and Home/End only engage on tighter ranges.
constexpr float kBoundSentinel = 1.0e29f;

constexpr float kHorizontalPadding = 10.0f;

bool IsContinuation(unsigned char byte) {
    return (byte & 0xC0u) == 0x80u;
}

std::size_t PreviousCodePoint(std::string_view text, std::size_t position) {
    if (position == 0) {
        return 0;
    }
    --position;
    while (position > 0 &&
           IsContinuation(static_cast<unsigned char>(text[position]))) {
        --position;
    }
    return position;
}

std::size_t NextCodePoint(std::string_view text, std::size_t position) {
    if (position >= text.size()) {
        return text.size();
    }
    ++position;
    while (position < text.size() &&
           IsContinuation(static_cast<unsigned char>(text[position]))) {
        ++position;
    }
    return position;
}

bool LooksLikeFloatPattern(const std::string& pattern) {
    const auto pct = pattern.find('%');
    if (pct == std::string::npos) {
        return false;
    }
    return pattern.find_first_of("fFeEgGaA", pct + 1) != std::string::npos;
}

} // namespace

std::shared_ptr<ScrubNumberField> ScrubNumberField::Create() {
    return std::shared_ptr<ScrubNumberField>(new ScrubNumberField());
}

ScrubNumberField::ScrubNumberField() {
    SetFocusable(true);
}

void ScrubNumberField::SetValue(float value) {
    const float clamped = std::clamp(value, minimum_, maximum_);
    if (clamped == value_) {
        return;
    }
    value_ = clamped;
    Invalidate();
    // Programmatic sets are never "committed" — the gesture callbacks own
    // that channel.
    if (on_value_change_) {
        on_value_change_(value_);
    }
}

void ScrubNumberField::SetMin(float minimum) {
    if (minimum > maximum_) {
        maximum_ = minimum;
    }
    minimum_ = minimum;
    SetValue(value_);
}

void ScrubNumberField::SetMax(float maximum) {
    if (maximum < minimum_) {
        minimum_ = maximum;
    }
    maximum_ = maximum;
    SetValue(value_);
}

void ScrubNumberField::SetRange(float minimum, float maximum) {
    if (minimum > maximum) {
        std::swap(minimum, maximum);
    }
    minimum_ = minimum;
    maximum_ = maximum;
    SetValue(value_);
}

void ScrubNumberField::SetStep(float step) {
    if (step > 0.0f) {
        step_ = step;
    }
}

void ScrubNumberField::SetSmallStep(float step) {
    if (step > 0.0f) {
        small_step_ = step;
    }
}

void ScrubNumberField::SetLargeStep(float step) {
    if (step > 0.0f) {
        large_step_ = step;
    }
}

void ScrubNumberField::SetPixelSensitivity(float px_per_step) {
    if (px_per_step > 0.0f) {
        pixel_sensitivity_ = px_per_step;
    }
}

void ScrubNumberField::SetAllowWheelScrub(bool allow) {
    allow_wheel_scrub_ = allow;
}

void ScrubNumberField::SetResetValue(float value) {
    reset_value_ = std::clamp(value, minimum_, maximum_);
}

void ScrubNumberField::ClearResetValue() {
    reset_value_.reset();
}

void ScrubNumberField::SetBoundFeedback(BoundFeedback feedback) {
    bound_feedback_ = feedback;
    Invalidate();
}

void ScrubNumberField::SetFormat(std::string printf_pattern) {
    if (!LooksLikeFloatPattern(printf_pattern)) {
        return;
    }
    format_ = std::move(printf_pattern);
    Invalidate();
}

void ScrubNumberField::SetValueFormatter(
    std::function<std::string(float)> formatter) {
    value_formatter_ = std::move(formatter);
    Invalidate();
}

void ScrubNumberField::SetUnit(std::string suffix) {
    if (unit_ == suffix) {
        return;
    }
    unit_ = std::move(suffix);
    Invalidate();
}

std::string ScrubNumberField::FormatValue(float value) const {
    if (value_formatter_) {
        return value_formatter_(value);
    }
    char buf[64];
    const int n = std::snprintf(buf, sizeof(buf), format_.c_str(),
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

void ScrubNumberField::SetOnValueChange(
    std::function<void(float)> callback) {
    on_value_change_ = std::move(callback);
}

void ScrubNumberField::SetOnValueCommitted(
    std::function<void(float)> callback) {
    on_value_committed_ = std::move(callback);
}

void ScrubNumberField::BeginEdit() {
    if (editing_ || !IsEnabled()) {
        return;
    }
    editing_ = true;
    // Show the full-precision value (no unit suffix, no clipped display)
    // and select it whole so typing replaces it outright.
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.9g", static_cast<double>(value_));
    edit_text_ = buf;
    caret_ = edit_text_.size();
    selection_anchor_ = 0;
    Invalidate();
}

float ScrubNumberField::ResolveStep(bool alt, bool shift) const {
    if (alt) {
        return small_step_;
    }
    if (shift) {
        return large_step_;
    }
    return step_;
}

void ScrubNumberField::ApplyGestureValue(float value) {
    const float previous = value_;
    const float clamped = std::clamp(value, minimum_, maximum_);
    if (clamped == previous) {
        // Still pin a bound flash when pushing past an already-pinned edge.
        if (scrubbing_ && (clamped == minimum_ || clamped == maximum_)) {
            if ((bound_feedback_ == BoundFeedback::Flash ||
                 bound_feedback_ == BoundFeedback::ShadeAndFlash) &&
                std::fabs(clamped) < kBoundSentinel) {
                if (value < clamped || value > clamped) {
                    TriggerBoundFlash(clamped == minimum_ ? -1.0f : 1.0f);
                }
            }
        }
        return;
    }
    value_ = clamped;
    Invalidate();
    if (on_value_change_) {
        on_value_change_(value_);
    }
    pending_commit_ = true;
    commit_idle_ = 0.0;

    if ((bound_feedback_ == BoundFeedback::Flash ||
         bound_feedback_ == BoundFeedback::ShadeAndFlash) &&
        std::fabs(clamped) < kBoundSentinel) {
        const bool at_min = clamped == minimum_ && value < previous;
        const bool at_max = clamped == maximum_ && value > previous;
        if (at_min) {
            TriggerBoundFlash(-1.0f);
        } else if (at_max) {
            TriggerBoundFlash(1.0f);
        }
    }
}

void ScrubNumberField::CommitGesture() {
    if (!pending_commit_) {
        return;
    }
    pending_commit_ = false;
    if (on_value_committed_) {
        on_value_committed_(value_);
    }
}

void ScrubNumberField::TriggerBoundFlash(float side) {
    bound_side_ = side;
    bound_flash_.from = 1.0f;
    bound_flash_.to = 0.0f;
    bound_flash_.duration = 0.35f;
    bound_flash_.t = Motion::ReducedMotion() ? 1.0f : 0.0f;
    Invalidate();
}

bool ScrubNumberField::ParseEditText(float* value_out) const {
    std::string text = edit_text_;
    const auto first = text.find_first_not_of(" \t");
    const auto last = text.find_last_not_of(" \t");
    if (first == std::string::npos) {
        return false;
    }
    text = text.substr(first, last - first + 1);
    // A trailing copy of the unit suffix is decoration, not a parse error.
    if (!unit_.empty() && text.size() > unit_.size() &&
        text.compare(text.size() - unit_.size(), unit_.size(), unit_) == 0) {
        text.erase(text.size() - unit_.size());
        const auto end = text.find_last_not_of(" \t");
        if (end == std::string::npos) {
            return false;
        }
        text.erase(end + 1);
    }
    float parsed = 0.0f;
    const char* begin = text.data();
    const char* end = begin + text.size();
    const auto result = std::from_chars(begin, end, parsed,
                                        std::chars_format::general);
    if (result.ec != std::errc() || result.ptr != end) {
        return false;
    }
    *value_out = parsed;
    return true;
}

void ScrubNumberField::CommitEdit() {
    if (!editing_) {
        return;
    }
    float parsed = 0.0f;
    editing_ = false;
    pending_caret_x_.reset();
    if (ParseEditText(&parsed)) {
        ApplyGestureValue(parsed);
    }
    CommitGesture();
    Invalidate();
}

void ScrubNumberField::CancelEdit() {
    if (!editing_) {
        return;
    }
    editing_ = false;
    pending_commit_ = false;
    pending_caret_x_.reset();
    Invalidate();
}

bool ScrubNumberField::HasEditSelection() const {
    return selection_anchor_ != caret_;
}

std::pair<std::size_t, std::size_t>
ScrubNumberField::EditSelectionRange() const {
    return {std::min(selection_anchor_, caret_),
            std::max(selection_anchor_, caret_)};
}

void ScrubNumberField::EraseEditRange(std::size_t begin, std::size_t end) {
    edit_text_.erase(begin, end - begin);
    caret_ = selection_anchor_ = begin;
    Invalidate();
}

void ScrubNumberField::MoveEditCaret(std::size_t position,
                                     bool extend_selection) {
    caret_ = std::min(position, edit_text_.size());
    if (!extend_selection) {
        selection_anchor_ = caret_;
    }
    Invalidate();
}

void ScrubNumberField::InsertEditText(const std::string& text) {
    std::string filtered;
    filtered.reserve(text.size());
    for (char c : text) {
        if (c == '\r' || c == '\n') {
            continue;
        }
        filtered += c;
    }
    if (filtered.empty()) {
        return;
    }
    if (HasEditSelection()) {
        const auto [begin, end] = EditSelectionRange();
        edit_text_.erase(begin, end - begin);
        caret_ = selection_anchor_ = begin;
    }
    edit_text_.insert(caret_, filtered);
    caret_ += filtered.size();
    selection_anchor_ = caret_;
    Invalidate();
}

bool ScrubNumberField::HandleEditKey(const Event& event) {
    if (event.type == Event::Type::TextInput) {
        InsertEditText(event.text);
        return !event.text.empty();
    }
    if (event.type != Event::Type::KeyDown) {
        return false;
    }

    if (event.is_ctrl && !event.is_alt) {
        switch (event.key) {
        case KeyCode::A:
            selection_anchor_ = 0;
            caret_ = edit_text_.size();
            Invalidate();
            return true;
        case KeyCode::C:
        case KeyCode::X:
            if (HasEditSelection()) {
                const auto [begin, end] = EditSelectionRange();
                ::XER::Content::Clipboard::SetText(
                    std::string_view(edit_text_).substr(begin, end - begin));
                if (event.key == KeyCode::X) {
                    EraseEditRange(begin, end);
                }
            }
            return true;
        case KeyCode::V: {
            std::string text;
            if (::XER::Content::Clipboard::GetText(text)) {
                InsertEditText(text);
            }
            return true;
        }
        default:
            break;
        }
    }

    const bool extend = event.is_shift;
    switch (event.key) {
    case KeyCode::Enter:
        CommitEdit();
        return true;
    case KeyCode::Escape:
        CancelEdit();
        return true;
    case KeyCode::Tab:
        // Commit, then let the tree cycle focus.
        CommitEdit();
        return false;
    case KeyCode::Backspace:
        if (HasEditSelection()) {
            const auto [b, e] = EditSelectionRange();
            EraseEditRange(b, e);
            return true;
        }
        if (caret_ > 0) {
            EraseEditRange(PreviousCodePoint(edit_text_, caret_), caret_);
            return true;
        }
        return false;
    case KeyCode::Delete:
        if (HasEditSelection()) {
            const auto [b, e] = EditSelectionRange();
            EraseEditRange(b, e);
            return true;
        }
        if (caret_ < edit_text_.size()) {
            EraseEditRange(caret_, NextCodePoint(edit_text_, caret_));
            return true;
        }
        return false;
    case KeyCode::ArrowLeft:
        if (HasEditSelection() && !extend) {
            MoveEditCaret(EditSelectionRange().first, false);
            return true;
        }
        if (caret_ > 0) {
            MoveEditCaret(PreviousCodePoint(edit_text_, caret_), extend);
            return true;
        }
        return false;
    case KeyCode::ArrowRight:
        if (HasEditSelection() && !extend) {
            MoveEditCaret(EditSelectionRange().second, false);
            return true;
        }
        if (caret_ < edit_text_.size()) {
            MoveEditCaret(NextCodePoint(edit_text_, caret_), extend);
            return true;
        }
        return false;
    case KeyCode::Home:
        MoveEditCaret(0, extend);
        return true;
    case KeyCode::End:
        MoveEditCaret(edit_text_.size(), extend);
        return true;
    default:
        return false;
    }
}

std::size_t ScrubNumberField::EditCaretIndexForX(
    float x,
    Rendering::Renderer& renderer,
    const Layout::Rect& text_bounds,
    const Rendering::TextStyle& style) const {
    const float local_x = x - text_bounds.x;
    if (local_x <= 0.0f) {
        return 0;
    }
    for (std::size_t pos = 0; pos <= edit_text_.size();) {
        const float width = renderer.MeasureText(
            std::string_view(edit_text_).substr(0, pos), style).width;
        if (width > local_x) {
            return pos == 0 ? 0 : PreviousCodePoint(edit_text_, pos);
        }
        if (pos == edit_text_.size()) {
            break;
        }
        pos = NextCodePoint(edit_text_, pos);
    }
    return edit_text_.size();
}

void ScrubNumberField::Render(Rendering::Renderer& renderer,
                              const Theme::Tokens& tokens) {
    const auto bounds = GetBounds();
    const bool enabled = IsEnabled();
    const bool focused = IsFocused();

    // Recessed field chrome, same family as TextInput.
    renderer.FillRect(bounds, enabled ? tokens.input : tokens.secondary,
                      tokens.radius_small);
    Theme::Color border = !enabled
        ? tokens.border
        : scrubbing_ ? tokens.accent
                     : (focused || editing_) ? tokens.ring
                          : (IsHovered() ? tokens.muted : tokens.border);
    renderer.StrokeRect(bounds, border,
                        (focused || editing_ || scrubbing_) ? 1.5f : 1.0f,
                        tokens.radius_small);

    const Rendering::TextStyle style{
        enabled ? tokens.foreground : tokens.disabled,
        tokens.font_body, 400};
    const auto text_bounds = CenteredTextRect(
        renderer, FormatValue(value_), style, kHorizontalPadding);

    renderer.PushClip(bounds);

    // Persistent bound shade: a soft cap fills the pinned edge so the
    // clamp is readable at a glance.
    if ((bound_feedback_ == BoundFeedback::Shade ||
         bound_feedback_ == BoundFeedback::ShadeAndFlash) &&
        std::fabs(value_) < kBoundSentinel) {
        constexpr float kCapWidth = 10.0f;
        Theme::Color shade = tokens.accent;
        shade.a = 0.18f;
        if (value_ == minimum_ && minimum_ > -kBoundSentinel) {
            renderer.FillRect({bounds.x, bounds.y, kCapWidth, bounds.height},
                              shade, 0.0f);
        } else if (value_ == maximum_ && maximum_ < kBoundSentinel) {
            renderer.FillRect({bounds.Right() - kCapWidth, bounds.y,
                               kCapWidth, bounds.height},
                              shade, 0.0f);
        }
    }

    if (editing_) {
        if (pending_caret_x_.has_value()) {
            caret_ = EditCaretIndexForX(*pending_caret_x_, renderer,
                                        text_bounds, style);
            selection_anchor_ = caret_;
            pending_caret_x_.reset();
        }
        const auto x_for = [&](std::size_t pos) {
            return std::clamp(
                text_bounds.x +
                    renderer.MeasureText(
                        std::string_view(edit_text_).substr(0, pos),
                        style).width,
                text_bounds.x, text_bounds.x + text_bounds.width);
        };
        if (HasEditSelection()) {
            const auto [begin, end] = EditSelectionRange();
            const float x0 = x_for(begin);
            const float x1 = x_for(end);
            Theme::Color highlight = tokens.accent;
            highlight.a = 0.35f;
            renderer.FillRect(
                {x0, text_bounds.y, x1 - x0, text_bounds.height},
                highlight, 0.0f);
        }
        renderer.DrawText(edit_text_, text_bounds, style);
        const float caret_x = x_for(caret_);
        renderer.DrawLine({caret_x, text_bounds.y},
                          {caret_x, text_bounds.y + text_bounds.height},
                          tokens.foreground, 1.0f);
    } else {
        // Value + muted unit suffix (formatter output is drawn whole).
        std::string numeric;
        if (value_formatter_) {
            numeric = FormatValue(value_);
        } else {
            char buf[64];
            const int n = std::snprintf(buf, sizeof(buf), format_.c_str(),
                                        static_cast<double>(value_));
            numeric = n > 0 ? std::string(buf, static_cast<std::size_t>(n))
                            : std::string();
        }
        renderer.DrawText(numeric, text_bounds, style);
        if (!unit_.empty() && !value_formatter_) {
            const float value_w =
                renderer.MeasureText(numeric, style).width;
            const Rendering::TextStyle unit_style{
                enabled ? tokens.muted : tokens.disabled,
                tokens.font_small, 400};
            renderer.DrawText(
                unit_,
                {text_bounds.x + value_w + 3.0f, text_bounds.y,
                 text_bounds.width - value_w - 3.0f, text_bounds.height},
                unit_style);
        }

        // Scrub affordance: edge chevrons appear on hover, accent while
        // the drag-scrub is engaged.
        if (enabled && (IsHovered() || scrubbing_)) {
            const Theme::Color chevron =
                scrubbing_ ? tokens.accent : tokens.muted;
            const float cy = bounds.y + bounds.height / 2.0f;
            constexpr float kChevronHalf = 3.5f;
            constexpr float kChevronInset = 4.0f;
            const float lx = bounds.x + kChevronInset;
            const float rx = bounds.Right() - kChevronInset;
            renderer.DrawLine({lx + kChevronHalf, cy - kChevronHalf},
                              {lx, cy}, chevron, 1.5f);
            renderer.DrawLine({lx, cy},
                              {lx + kChevronHalf, cy + kChevronHalf},
                              chevron, 1.5f);
            renderer.DrawLine({rx - kChevronHalf, cy - kChevronHalf},
                              {rx, cy}, chevron, 1.5f);
            renderer.DrawLine({rx, cy},
                              {rx - kChevronHalf, cy + kChevronHalf},
                              chevron, 1.5f);
        }
    }
    renderer.PopClip();

    // Transient bound flash: accent edge pulse on the side that hit its
    // clamp. Reduced motion collapses the tween so nothing paints.
    const float flash = bound_flash_.Value();
    if (flash > 0.001f && bound_side_ != 0.0f) {
        Theme::Color flash_color = tokens.accent;
        flash_color.a = 0.5f * flash;
        renderer.StrokeRect(bounds, flash_color, 2.0f,
                            tokens.radius_small);
        const float cap_x = bound_side_ < 0.0f
            ? bounds.x
            : bounds.Right() - 4.0f;
        renderer.FillRect({cap_x, bounds.y + 2.0f, 4.0f,
                           bounds.height - 4.0f},
                          flash_color, 2.0f);
    }

    RenderFocusRing(renderer, tokens, tokens.radius_small);
}

void ScrubNumberField::Layout(const Layout::Rect& bounds) {
    SetBounds(bounds);
}

void ScrubNumberField::Tick(double dt_seconds) {
    if (!IsEffectivelyVisible()) {
        return;
    }
    bound_flash_.Update(static_cast<float>(dt_seconds));
    if (bound_flash_.Active()) {
        Invalidate();
    }

    // Focus transitions are flag-based in the tree (no Blur event on
    // widget focus change), so poll the edge here: losing focus is a
    // commit — the edit text applies, a pending nudge lands.
    if (was_focused_ && !IsFocused()) {
        if (editing_) {
            CommitEdit();
        } else {
            CommitGesture();
        }
    }
    was_focused_ = IsFocused();

    // Debounce is measured in tick time (not wall clock) so a headless
    // Tick() drives it deterministically.
    if (pending_commit_ && !scrubbing_) {
        commit_idle_ += dt_seconds;
        if (commit_idle_ >= kCommitDebounceSeconds) {
            CommitGesture();
        }
    }
}

bool ScrubNumberField::HandleEvent(const Event& event) {
    if (!IsEnabled()) {
        return false;
    }

    // Window-level blur: commit the same way tree-focus loss does.
    if (event.type == Event::Type::Blur) {
        pressed_ = false;
        scrubbing_ = false;
        if (editing_) {
            CommitEdit();
        } else {
            CommitGesture();
        }
        return false;
    }

    // Click handling (including the double-click reset) runs before the
    // editing branch — the first click may have opened the editor, and the
    // second click of a double-click must still reach the reset path.
    if (event.type == Event::Type::Click &&
        event.mouse_button == MouseButton::Left) {
        if (just_scrubbed_) {
            just_scrubbed_ = false;
            // The scrub-release click is fully consumed — it must not
            // seed the double-click clock, or the next click would be
            // misread as the second half of a pair.
            last_click_ = std::chrono::steady_clock::time_point{};
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
        if (double_click && reset_value_.has_value()) {
            last_click_ = std::chrono::steady_clock::time_point{};
            CancelEdit();
            ApplyGestureValue(*reset_value_);
            CommitGesture();
            return true;
        }
        if (!editing_) {
            BeginEdit();
        }
        return true;
    }

    if (editing_) {
        // While editing, mouse input only positions the caret — no scrub.
        if (event.type == Event::Type::MouseDown &&
            event.mouse_button == MouseButton::Left) {
            pending_caret_x_ = event.position.x;
            Invalidate();
            return true;
        }
        // Swallow stray MouseUp so it never leaks to parent handlers
        // behind the field.
        if (event.type == Event::Type::MouseUp) {
            return true;
        }
        return HandleEditKey(event);
    }

    switch (event.type) {
    case Event::Type::MouseDown:
        if (event.mouse_button != MouseButton::Left) {
            return false;
        }
        pressed_ = true;
        just_scrubbed_ = false;
        press_x_ = event.position.x;
        anchor_dx_ = 0.0f;
        anchor_value_ = value_;
        anchor_alt_ = event.is_alt;
        anchor_shift_ = event.is_shift;
        return true;

    case Event::Type::MouseMove: {
        if (!pressed_ || event.mouse_button != MouseButton::Left) {
            return false;
        }
        const float dx = event.position.x - press_x_;
        if (!scrubbing_) {
            if (std::fabs(dx) < kDragThreshold) {
                return true;
            }
            scrubbing_ = true;
            // The drag threshold only delays ENGAGEMENT — all travel from
            // the press point still counts, so anchor_dx_ stays zero.
            anchor_dx_ = 0.0f;
            anchor_value_ = value_;
            anchor_alt_ = event.is_alt;
            anchor_shift_ = event.is_shift;
        }
        // Modifier flip mid-drag re-anchors so the accumulated travel is
        // reinterpreted under the new step — no value jump.
        if (event.is_alt != anchor_alt_ || event.is_shift != anchor_shift_) {
            anchor_dx_ = dx;
            anchor_value_ = value_;
            anchor_alt_ = event.is_alt;
            anchor_shift_ = event.is_shift;
        }
        const float step = ResolveStep(anchor_alt_, anchor_shift_);
        const float steps = (dx - anchor_dx_) / pixel_sensitivity_;
        ApplyGestureValue(anchor_value_ + std::round(steps) * step);
        return true;
    }

    case Event::Type::MouseUp:
        if (event.mouse_button != MouseButton::Left) {
            return false;
        }
        pressed_ = false;
        if (scrubbing_) {
            scrubbing_ = false;
            just_scrubbed_ = true;
            CommitGesture();
            Invalidate();
            return true;
        }
        return false;

    case Event::Type::MouseWheel:
        if (!allow_wheel_scrub_ || !IsFocused()) {
            return false;
        }
        if (event.wheel_delta != 0) {
            const float notches =
                static_cast<float>(event.wheel_delta) / 120.0f;
            ApplyGestureValue(
                value_ + notches * ResolveStep(event.is_alt, event.is_shift));
            return true;
        }
        return false;

    case Event::Type::KeyDown:
        if (!IsFocused()) {
            return false;
        }
        switch (event.key) {
        case KeyCode::ArrowUp:
        case KeyCode::ArrowRight:
            ApplyGestureValue(
                value_ + ResolveStep(event.is_alt, event.is_shift));
            return true;
        case KeyCode::ArrowDown:
        case KeyCode::ArrowLeft:
            ApplyGestureValue(
                value_ - ResolveStep(event.is_alt, event.is_shift));
            return true;
        case KeyCode::PageUp:
            ApplyGestureValue(value_ + large_step_);
            return true;
        case KeyCode::PageDown:
            ApplyGestureValue(value_ - large_step_);
            return true;
        case KeyCode::Home:
            if (minimum_ > -kBoundSentinel) {
                ApplyGestureValue(minimum_);
                CommitGesture();
                return true;
            }
            return false;
        case KeyCode::End:
            if (maximum_ < kBoundSentinel) {
                ApplyGestureValue(maximum_);
                CommitGesture();
                return true;
            }
            return false;
        case KeyCode::Enter:
            BeginEdit();
            return true;
        case KeyCode::Delete:
        case KeyCode::Backspace:
            if (reset_value_.has_value()) {
                ApplyGestureValue(*reset_value_);
                CommitGesture();
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

Layout::Size ScrubNumberField::GetMinSize() const {
    return {64.0f, 24.0f};
}

Layout::Size ScrubNumberField::GetPreferredSize() const {
    return {96.0f, 28.0f};
}

Layout::Size ScrubNumberField::GetMaxSize() const {
    return {100000.0f, 100000.0f};
}

std::string ScrubNumberField::GetDebugText() const {
    return FormatValue(value_);
}

} // namespace XER::UI::Widgets
