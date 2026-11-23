#include "confirm.h"

#include <algorithm>
#include <utility>

#include "motion/motion.h"
#include "rendering/renderer.h"
#include "theme/tokens.h"

namespace XER::UI::Widgets {
namespace {

// Per-size metrics (DIP) — same geometry as Button so a ConfirmButton
// drops into any slot a Button would.
constexpr float kHeightSmall = 28.0f;
constexpr float kHeightDefault = 36.0f;
constexpr float kHeightLarge = 44.0f;
constexpr float kMinWidthSmall = 56.0f;
constexpr float kMinWidthDefault = 64.0f;
constexpr float kMinWidthLarge = 80.0f;

} // namespace

ConfirmButton::ConfirmButton(std::string text, ButtonStyle style)
    : text_(std::move(text))
    , style_(style) {
    SetFocusable(true);
    press_.Snap(0.0f);
    hover_t_.Set(0.0f);
    arm_t_.Set(0.0f);
}

void ConfirmButton::SetText(std::string text) {
    if (text_ == text) {
        return;
    }
    text_ = std::move(text);
    Invalidate();
    RequestLayout();
}

void ConfirmButton::SetOnConfirm(std::function<void()> callback) {
    on_confirm_ = std::move(callback);
}

void ConfirmButton::SetStyle(ButtonStyle style) {
    style_ = style;
    Invalidate();
}

void ConfirmButton::SetSize(ButtonSize size) {
    if (size_ == size) {
        return;
    }
    size_ = size;
    Invalidate();
    RequestLayout();
}

void ConfirmButton::SetConfirmMode(ConfirmMode mode) {
    if (mode_ == mode) {
        return;
    }
    Disarm();
    mode_ = mode;
    Invalidate();
}

void ConfirmButton::SetArmedText(std::string text) {
    armed_text_ = std::move(text);
    if (armed_ && mode_ == ConfirmMode::ClickAgain) {
        Invalidate();
    }
}

void ConfirmButton::SetArmTimeout(double seconds) {
    arm_timeout_ = std::max(0.2, seconds);
}

void ConfirmButton::SetHoldDuration(double seconds) {
    hold_duration_ = std::max(0.15, seconds);
}

float ConfirmButton::HoldProgress() const {
    if (hold_duration_ <= 0.0) {
        return 0.0f;
    }
    return std::clamp(
        static_cast<float>(hold_elapsed_ / hold_duration_), 0.0f, 1.0f);
}

void ConfirmButton::SetConfirmPhrase(std::string phrase) {
    phrase_ = std::move(phrase);
    Invalidate();
    RequestLayout();
}

void ConfirmButton::Disarm() {
    if (!armed_ && typed_.empty() && !holding_) {
        return;
    }
    armed_ = false;
    holding_ = false;
    hold_elapsed_ = 0.0;
    arm_elapsed_ = 0.0;
    typed_.clear();
    arm_t_.Set(0.0f, 0.10f);
    Invalidate();
}

void ConfirmButton::Fire(bool swallow_trailing_click) {
    Disarm();
    suppress_click_pending_ = swallow_trailing_click;
    if (on_confirm_) {
        on_confirm_();
    }
}

float ConfirmButton::HeightForSize() const {
    switch (size_) {
        case ButtonSize::Small: return kHeightSmall;
        case ButtonSize::Large: return kHeightLarge;
        case ButtonSize::Default:
        default: return kHeightDefault;
    }
}

std::size_t ConfirmButton::Utf8Length(std::string_view text) const {
    std::size_t count = 0;
    for (const unsigned char byte : text) {
        if ((byte & 0xC0u) != 0x80u) {
            ++count;
        }
    }
    return count;
}

void ConfirmButton::Activate() {
    switch (mode_) {
        case ConfirmMode::ClickAgain:
            if (!armed_) {
                armed_ = true;
                arm_elapsed_ = 0.0;
                arm_t_.Set(1.0f, 0.15f);
                Invalidate();
            } else {
                Fire(false);
            }
            return;
        case ConfirmMode::HoldToConfirm:
            // Pointerless activation (Enter/Space, or a plain click
            // when the hold itself didn't reach the threshold): arm —
            // a second activation confirms.
            if (!armed_) {
                armed_ = true;
                arm_elapsed_ = 0.0;
                arm_t_.Set(1.0f, 0.15f);
                Invalidate();
            } else {
                Fire(false);
            }
            return;
        case ConfirmMode::TypePhrase:
            if (!armed_) {
                armed_ = true;
                arm_elapsed_ = 0.0;
                typed_.clear();
                arm_t_.Set(1.0f, 0.15f);
                Invalidate();
            }
            // While armed, clicks keep the field armed (the phrase
            // itself is the confirmation gesture).
            return;
    }
}

void ConfirmButton::Tick(double dt_seconds) {
    if (!IsEffectivelyVisible()) {
        return;
    }
    // ClickAway disarm: losing widget focus reverts an armed control
    // (sureui's click-away cancel). Focus loss is flag-based in this
    // kit — compare against the previous tick.
    if (was_focused_ && !IsFocused() && armed_) {
        Disarm();
    }
    was_focused_ = IsFocused();

    const float want_hover = IsHovered() && IsEnabled() ? 1.0f : 0.0f;
    if (hover_t_.to != want_hover) {
        hover_t_.Set(want_hover, 0.12f);
    }
    hover_t_.Update(static_cast<float>(dt_seconds));
    press_.Update(static_cast<float>(dt_seconds));
    arm_t_.Update(static_cast<float>(dt_seconds));

    if (armed_ && mode_ != ConfirmMode::TypePhrase) {
        arm_elapsed_ += dt_seconds;
        if (arm_elapsed_ >= arm_timeout_) {
            Disarm();
        }
    }
    if (holding_) {
        hold_elapsed_ += dt_seconds;
        if (hold_elapsed_ >= hold_duration_) {
            holding_ = false;
            hold_elapsed_ = 0.0;
            // The press is still down — its release synthesizes a Click
            // that must not re-arm in the same breath.
            Fire(true);
        }
        Invalidate();
    }
    if (hover_t_.Active() || !press_.Settled() || arm_t_.Active()) {
        Invalidate();
    }
}

void ConfirmButton::Render(Rendering::Renderer& renderer,
                           const Theme::Tokens& tokens) {
    const auto bounds = GetBounds();
    const float press = press_.pos;
    const float hov = hover_t_.Value();
    const float arm = arm_t_.Value();
    const bool enabled = IsEnabled();

    Layout::Rect surface = bounds;
    surface.y += press * 1.5f;
    surface.height = std::max(1.0f, surface.height - press * 1.5f);
    const float radius = tokens.radius_medium;

    const auto lerp = [](const Theme::Color& a, const Theme::Color& b,
                         float t) {
        return Motion::LerpColor(a, b, t);
    };

    Theme::Color fill_color{};
    Theme::Color stroke_color{};
    Theme::Color text_color{};
    bool has_fill = true;
    bool has_stroke = false;

    const bool phrase_armed = armed_ && mode_ == ConfirmMode::TypePhrase;

    if (!enabled) {
        const bool flat = style_ == ButtonStyle::Ghost ||
                          style_ == ButtonStyle::Outline;
        has_fill = !flat;
        fill_color = tokens.secondary;
        stroke_color = tokens.border;
        text_color = tokens.disabled;
        has_stroke = !flat || style_ == ButtonStyle::Outline;
    } else if (phrase_armed) {
        // TypePhrase armed: the control becomes an inline field —
        // recessed input fill + focus ring stroke.
        fill_color = tokens.input;
        stroke_color = tokens.ring;
        text_color = tokens.foreground;
        has_stroke = true;
    } else {
        switch (style_) {
            case ButtonStyle::Primary:
                fill_color = lerp(tokens.accent, tokens.foreground,
                                  hov * 0.14f);
                fill_color.a *= 1.0f - hov * 0.05f;
                text_color = tokens.accent_foreground;
                break;
            case ButtonStyle::Secondary:
                fill_color = lerp(tokens.secondary, tokens.pressed,
                                  std::max(hov, press));
                text_color = tokens.foreground;
                break;
            case ButtonStyle::Outline:
                has_fill = hov > 0.01f || press > 0.01f || arm > 0.01f;
                fill_color = lerp(
                    lerp(tokens.surface, tokens.hover, hov),
                    tokens.pressed, press * 0.7f);
                stroke_color = tokens.border;
                text_color = tokens.foreground;
                has_stroke = true;
                break;
            case ButtonStyle::Ghost:
                has_fill = hov > 0.01f || press > 0.01f || arm > 0.01f;
                fill_color = lerp(
                    lerp(tokens.surface, tokens.hover, hov),
                    tokens.pressed, press * 0.7f);
                text_color = lerp(tokens.muted, tokens.foreground,
                                  std::max(hov, press));
                break;
            case ButtonStyle::Destructive:
                fill_color = lerp(tokens.danger, tokens.danger_foreground,
                                  hov * 0.12f);
                fill_color.a *= 1.0f - hov * 0.05f;
                text_color = tokens.danger_foreground;
                break;
        }
        // Armed (ClickAgain / hold-armed): warning-tinted chrome —
        // warning_chip border + a low-alpha warning wash over the fill
        // so the "are you sure?" state reads at a glance.
        if (armed_) {
            stroke_color = tokens.warning_chip;
            has_stroke = true;
            Theme::Color warn = tokens.warning_chip;
            warn.a *= 0.18f * arm;
            fill_color = lerp(fill_color, warn, std::min(1.0f, arm));
            has_fill = true;
        }
    }

    if (has_fill) {
        renderer.FillRect(surface, fill_color, radius);
    }
    if (has_stroke) {
        renderer.StrokeRect(surface, stroke_color,
                            armed_ || phrase_armed ? 1.5f : 1.0f, radius);
    }

    // HoldToConfirm progress: danger fill sweeping left→right while the
    // press is held. This is user-driven progress (not decorative
    // motion) so it renders the same under ReducedMotion.
    const float hold_p = holding_ ? HoldProgress() : 0.0f;
    if (hold_p > 0.0f) {
        Theme::Color prog = tokens.danger;
        prog.a *= 0.30f;
        renderer.PushClip(surface);
        renderer.FillRect(
            {surface.x, surface.y, surface.width * hold_p,
             surface.height},
            prog, 0.0f);
        // Leading edge tick.
        renderer.DrawLine(
            {surface.x + surface.width * hold_p, surface.y},
            {surface.x + surface.width * hold_p,
             surface.y + surface.height},
            tokens.danger, 2.0f);
        renderer.PopClip();
    }

    if (press > 0.01f && enabled) {
        Theme::Color shade = tokens.foreground;
        shade.a = 0.07f * press;
        renderer.FillRect(surface, shade, radius);
    }

    // Label / armed phrase field.
    const float min_pad = size_ == ButtonSize::Small ? 6.0f : 8.0f;
    if (phrase_armed) {
        // Inline phrase field: typed text + caret, or the muted hint.
        const Rendering::TextStyle field_style{
            tokens.foreground, tokens.font_body, 400};
        const auto sample = typed_.empty()
            ? std::string_view("Ag")
            : std::string_view(typed_);
        const float text_h = renderer.MeasureText(sample, field_style).height;
        const float y = surface.y +
            std::max(0.0f, (surface.height - text_h) * 0.5f);
        renderer.PushClip(surface);
        if (typed_.empty()) {
            const std::string hint =
                phrase_.empty() ? "Type phrase to confirm"
                                : "Type " + phrase_ + " to confirm";
            const Rendering::TextStyle hint_style{
                tokens.muted, tokens.font_small, 400};
            renderer.DrawText(
                hint,
                Layout::Rect{surface.x + 10.0f, y,
                             std::max(0.0f, surface.width - 20.0f),
                             text_h + 1.0f},
                hint_style);
        } else {
            const float text_w = renderer.MeasureText(typed_, field_style).width;
            const bool matched = !phrase_.empty() && typed_ == phrase_;
            const Rendering::TextStyle typed_style{
                matched ? tokens.danger : tokens.foreground,
                tokens.font_body, matched ? 600 : 400};
            renderer.DrawText(
                typed_,
                Layout::Rect{surface.x + 10.0f, y,
                             std::max(0.0f, surface.width - 20.0f),
                             text_h + 1.0f},
                typed_style);
            const float caret_x = surface.x + 10.0f + text_w + 1.0f;
            renderer.DrawLine(
                {caret_x, y}, {caret_x, y + text_h},
                tokens.foreground, 1.0f);
        }
        renderer.PopClip();
    } else {
        const std::string& label = armed_ ? armed_text_ : text_;
        Theme::Color label_color = text_color;
        if (armed_ && enabled && style_ != ButtonStyle::Destructive) {
            label_color = lerp(text_color, tokens.danger, 0.85f);
        }
        const Rendering::TextStyle style{label_color, tokens.font_body, 500};
        const auto sample = label.empty()
            ? std::string_view("Ag")
            : std::string_view(label);
        const auto measured = renderer.MeasureText(sample, style);
        const float y = surface.y +
            std::max(0.0f, (surface.height - measured.height) * 0.5f);
        renderer.DrawText(
            label,
            Layout::Rect{surface.x + min_pad +
                             std::max(0.0f, (surface.width - 2.0f * min_pad -
                                             measured.width) * 0.5f),
                         y, measured.width + 2.0f,
                         measured.height + 1.0f},
            style);
    }

    RenderFocusRing(renderer, tokens, radius);
}

void ConfirmButton::Layout(const Layout::Rect& bounds) {
    SetBounds(bounds);
}

bool ConfirmButton::HandleEvent(const Event& event) {
    if (!IsEnabled()) {
        if (held_ || holding_) {
            held_ = false;
            holding_ = false;
            press_.SetTarget(0.0f);
        }
        return false;
    }

    switch (event.type) {
        case Event::Type::MouseDown:
            if (event.mouse_button == MouseButton::Left) {
                // Every press re-arms the click-suppression flag (same
                // rule as ContextMenu): a stale flag must not eat this
                // gesture's click.
                suppress_click_pending_ = false;
                held_ = true;
                press_.SetTarget(1.0f);
                if (mode_ == ConfirmMode::HoldToConfirm) {
                    holding_ = true;
                    hold_elapsed_ = 0.0;
                }
                Invalidate();
                return true;
            }
            return false;
        case Event::Type::MouseUp:
            if (event.mouse_button == MouseButton::Left) {
                held_ = false;
                holding_ = false;
                hold_elapsed_ = 0.0; // early release: progress resets
                press_.SetTarget(0.0f);
                Invalidate();
                return true;
            }
            return false;
        case Event::Type::Click:
            if (event.mouse_button != MouseButton::Left) {
                return false;
            }
            held_ = false;
            press_.SetTarget(0.0f);
            if (suppress_click_pending_) {
                suppress_click_pending_ = false;
                return true;
            }
            // TypePhrase: a click while armed only keeps the field (the
            // phrase itself confirms).
            if (armed_ && mode_ == ConfirmMode::TypePhrase) {
                return true;
            }
            Activate();
            return true;
        case Event::Type::TextInput:
            if (armed_ && mode_ == ConfirmMode::TypePhrase) {
                // Phrase draft: literal characters only — the input
                // translator already filters control chars.
                for (const char c : event.text) {
                    if (c == '\r' || c == '\n' || c == '\t') {
                        continue;
                    }
                    typed_ += c;
                }
                Invalidate();
                // Exact match fires immediately — the phrase itself
                // is the confirmation.
                if (!phrase_.empty() && typed_ == phrase_) {
                    Fire(false);
                }
                return true;
            }
            return false;
        case Event::Type::KeyDown:
            if (event.key == KeyCode::Escape && armed_) {
                Disarm();
                return true;
            }
            if (event.key == KeyCode::Backspace && armed_ &&
                mode_ == ConfirmMode::TypePhrase) {
                // Drop the last UTF-8 code point.
                while (!typed_.empty() &&
                       (static_cast<unsigned char>(typed_.back()) &
                        0xC0u) == 0x80u) {
                    typed_.pop_back();
                }
                if (!typed_.empty()) {
                    typed_.pop_back();
                }
                Invalidate();
                return true;
            }
            if (IsFocused() &&
                (event.key == KeyCode::Enter ||
                 event.key == KeyCode::Space)) {
                Activate();
                return true;
            }
            return false;
        case Event::Type::Blur:
            // Window deactivated — cancel any pending gesture.
            Disarm();
            held_ = false;
            holding_ = false;
            return false;
        default:
            return false;
    }
}

Layout::Size ConfirmButton::GetMinSize() const {
    const float w = size_ == ButtonSize::Small ? kMinWidthSmall
                    : size_ == ButtonSize::Large ? kMinWidthLarge
                                                 : kMinWidthDefault;
    return {w, HeightForSize()};
}

Layout::Size ConfirmButton::GetPreferredSize() const {
    // Widest label the control can show wins so arming never reflows
    // the layout (same fixed-geometry rule as FaderValue).
    std::size_t chars = Utf8Length(text_);
    if (mode_ == ConfirmMode::TypePhrase && !phrase_.empty()) {
        chars = std::max(chars, Utf8Length(phrase_) + 16);
    }
    chars = std::max(chars, Utf8Length(armed_text_));
    return {
        std::max(GetMinSize().width,
                 static_cast<float>(chars) * 7.7f + 32.0f),
        HeightForSize()
    };
}

Layout::Size ConfirmButton::GetMaxSize() const {
    return {100000.0f, 100000.0f};
}

} // namespace XER::UI::Widgets
