#include "button.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "motion/motion.h"
#include "rendering/renderer.h"
#include "theme/tokens.h"

namespace XER::UI::Widgets {
namespace {

std::size_t Utf8Length(std::string_view text) {
    std::size_t count = 0;
    for (const unsigned char byte : text) {
        if ((byte & 0xC0u) != 0x80u) {
            ++count;
        }
    }
    return count;
}

constexpr float kPi = 3.14159265358979323846f;

} // namespace

Button::Button(std::string text, ButtonStyle style)
    : text_(std::move(text))
    , style_(style) {
    SetFocusable(true);
    press_.Snap(0.0f);
    hover_t_.Set(0.0f);
}

void Button::SetText(std::string text) {
    if (text_ == text) {
        return;
    }
    text_ = std::move(text);
    Invalidate();
    RequestLayout();
}

const std::string& Button::GetText() const {
    return text_;
}

void Button::SetOnClick(std::function<void()> callback) {
    on_click_ = std::move(callback);
}

void Button::SetStyle(ButtonStyle style) {
    style_ = style;
    Invalidate();
}

void Button::SetLoading(bool loading) {
    if (loading_ == loading) {
        return;
    }
    loading_ = loading;
    Invalidate();
}

void Button::Tick(double dt_seconds) {
    if (!IsEffectivelyVisible()) {
        return;
    }
    const float want_hover = IsHovered() && IsEnabled() ? 1.0f : 0.0f;
    if (hover_t_.to != want_hover) {
        hover_t_.Set(want_hover, 0.12f);
    }
    hover_t_.Update(static_cast<float>(dt_seconds));
    press_.Update(static_cast<float>(dt_seconds));
    if (loading_) {
        spinner_angle_ = std::fmod(spinner_angle_ +
                                      static_cast<float>(dt_seconds) * 1.4f,
                                  1.0f);
    }
    if (hover_t_.Active() || !press_.Settled() || loading_) {
        Invalidate();
    }
}

void Button::Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) {
    const auto bounds = GetBounds();
    const float press = press_.pos;
    const float hov = hover_t_.Value();

    // Press depth: the whole surface sinks ~1.5px while held.
    Layout::Rect surface = bounds;
    surface.y += press * 1.5f;
    surface.height = std::max(1.0f, surface.height - press * 1.5f);
    const float radius = tokens.radius_medium;

    Theme::Color fill_color{};
    Theme::Color stroke_color{};
    Theme::Color text_color{};
    bool has_fill = true;
    bool has_stroke = false;

    const auto lerp = [](const Theme::Color& a, const Theme::Color& b, float t) {
        return Motion::LerpColor(a, b, t);
    };

    if (!IsEnabled()) {
        fill_color = tokens.muted;
        stroke_color = tokens.border;
        text_color = tokens.background;
        has_stroke = true;
    } else {
        // Loading keeps the resting style colors; the spinner carries state.
        switch (style_) {
            case ButtonStyle::Primary:
                // Tailwind/Headless UI primary: accent fill. Hover deepens
                // toward foreground — darkens indigo-600 on light, lightens
                // indigo-500 on dark (matches Tailwind hover direction).
                fill_color = lerp(tokens.accent, tokens.foreground, hov * 0.14f);
                fill_color.a *= 1.0f - hov * 0.05f;
                text_color = tokens.accent_foreground;
                break;
            case ButtonStyle::Secondary:
                fill_color = lerp(tokens.surface, tokens.hover, hov);
                stroke_color = tokens.border;
                text_color = tokens.foreground;
                has_stroke = true;
                break;
            case ButtonStyle::Outline:
                has_fill = hov > 0.01f;
                fill_color = lerp(tokens.surface, tokens.hover, hov);
                stroke_color = tokens.border;
                text_color = tokens.foreground;
                has_stroke = true;
                break;
            case ButtonStyle::Ghost:
                has_fill = hov > 0.01f;
                fill_color = lerp(tokens.surface, tokens.hover, hov);
                text_color = tokens.foreground;
                break;
            case ButtonStyle::Destructive:
                fill_color = lerp(tokens.danger, tokens.danger_foreground, hov * 0.12f);
                fill_color.a *= 1.0f - hov * 0.05f;
                text_color = tokens.danger_foreground;
                break;
        }
    }

    if (has_fill) {
        renderer.FillRect(surface, fill_color, radius);
    }
    if (has_stroke) {
        renderer.StrokeRect(surface, stroke_color, 1.0f, radius);
    }
    // Press shading: a faint foreground wash sells the depth without a shadow
    // primitive (works on both light and dark themes).
    if (press > 0.01f && IsEnabled()) {
        Theme::Color shade = tokens.foreground;
        shade.a = 0.07f * press;
        renderer.FillRect(surface, shade, radius);
    }

    const Rendering::TextStyle style{text_color, tokens.font_body, 500};
    // Loading shifts the label right so it clears the spinner; the preferred
    // size is unchanged, so the button never resizes.
    const float text_pad = tokens.spacing_medium + (loading_ ? 16.0f : 0.0f);
    Layout::Rect text_rect = CenteredTextRect(renderer, text_, style, text_pad);
    if (loading_) {
        text_rect.x += 8.0f;
        text_rect.width = std::max(0.0f, text_rect.width - 16.0f);
    }
    renderer.DrawText(text_, text_rect, style);

    if (loading_) {
        // 8-spoke arc spinner, same visual language as the Spinner widget.
        const float cx = bounds.x + 16.0f;
        const float cy = bounds.y + bounds.height * 0.5f;
        const float r_out = 5.0f;
        const float r_in = r_out * 0.45f;
        const float angle_offset = spinner_angle_ * 2.0f * kPi;
        for (int spoke = 0; spoke < 8; ++spoke) {
            const float angle = angle_offset +
                2.0f * kPi * static_cast<float>(spoke) / 8.0f;
            Theme::Color spoke_color = tokens.muted;
            spoke_color.a = 0.2f + 0.8f * static_cast<float>(spoke) / 7.0f;
            renderer.DrawLine(
                {cx + std::cos(angle) * r_in, cy + std::sin(angle) * r_in},
                {cx + std::cos(angle) * r_out, cy + std::sin(angle) * r_out},
                spoke_color, 1.6f);
        }
    }
    RenderFocusRing(renderer, tokens, radius);
}

void Button::Layout(const Layout::Rect& bounds) {
    SetBounds(bounds);
}

bool Button::HandleEvent(const Event& event) {
    if (!IsEnabled()) {
        return false;
    }

    switch (event.type) {
        case Event::Type::MouseDown:
            if (event.mouse_button == MouseButton::Left) {
                held_ = true;
                press_.SetTarget(1.0f);
                Invalidate();
                return true;
            }
            return false;
        case Event::Type::MouseUp:
            if (held_ && event.mouse_button == MouseButton::Left) {
                held_ = false;
                press_.SetTarget(0.0f);
                Invalidate();
                return true;
            }
            return false;
        case Event::Type::MouseMove:
            // Releasing outside the button still ends the press (mouse capture
            // delivers the up to us); nothing else to do here.
            return false;
        case Event::Type::Click:
            if (event.mouse_button == MouseButton::Left) {
                held_ = false;
                press_.SetTarget(0.0f);
                if (loading_) {
                    return true; // busy: consume but don't fire
                }
                Activate();
                return true;
            }
            return false;
        case Event::Type::KeyDown:
            if (IsFocused() &&
                (event.key == KeyCode::Enter || event.key == KeyCode::Space)) {
                if (!loading_) {
                    Activate();
                }
                return true;
            }
            return false;
        default:
            return false;
    }
}

Layout::Size Button::GetMinSize() const {
    return {64.0f, 36.0f};
}

Layout::Size Button::GetPreferredSize() const {
    return {
        std::max(GetMinSize().width,
                 static_cast<float>(Utf8Length(text_)) * 7.7f +
                     32.0f + (loading_ ? 16.0f : 0.0f)),
        36.0f
    };
}

Layout::Size Button::GetMaxSize() const {
    return {100000.0f, 100000.0f};
}

void Button::Activate() {
    if (on_click_) {
        on_click_();
    }
}

} // namespace XER::UI::Widgets
