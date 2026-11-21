#include "badge.h"

#include <algorithm>
#include <utility>

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

} // namespace

Badge::Badge(std::string text, BadgeVariant variant)
    : text_(std::move(text))
    , variant_(variant) {
    SetFocusable(false);
}

void Badge::SetText(std::string text) {
    if (text_ == text) {
        return;
    }
    text_ = std::move(text);
    Invalidate();
    RequestLayout();
}

const std::string& Badge::GetText() const {
    return text_;
}

void Badge::SetVariant(BadgeVariant variant) {
    if (variant_ == variant) {
        return;
    }
    variant_ = variant;
    Invalidate();
}

BadgeVariant Badge::GetVariant() const {
    return variant_;
}

void Badge::Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) {
    const auto bounds = GetBounds();

    Theme::Color fill_color{};
    Theme::Color stroke_color{};
    Theme::Color text_color{};
    bool has_fill = true;
    bool has_stroke = false;
    switch (variant_) {
        case BadgeVariant::Default:
            fill_color = tokens.foreground;
            text_color = tokens.surface;
            break;
        case BadgeVariant::Secondary:
            fill_color = tokens.surface;
            stroke_color = tokens.border;
            text_color = tokens.foreground;
            has_stroke = true;
            break;
        case BadgeVariant::Outline:
            has_fill = false;
            stroke_color = tokens.border;
            text_color = tokens.foreground;
            has_stroke = true;
            break;
        case BadgeVariant::Accent:
            fill_color = tokens.accent;
            text_color = tokens.accent_foreground;
            break;
        case BadgeVariant::Destructive:
            fill_color = tokens.danger;
            text_color = tokens.danger_foreground;
            break;
    }
    if (!IsEnabled()) {
        fill_color = tokens.secondary;
        stroke_color = tokens.border;
        text_color = tokens.disabled;
        has_stroke = true;
        has_fill = true;
    }

    // Chip rounding: ~1/3 of the badge height (clamped to radius_medium)
    // reads as a small rounded chip rather than a square tag.
    const float chip_radius =
        std::min(bounds.height * 0.35f, tokens.radius_medium);
    if (has_fill) {
        renderer.FillRect(bounds, fill_color, chip_radius);
    }
    if (has_stroke) {
        renderer.StrokeRect(bounds, stroke_color, 1.0f, chip_radius);
    }
    const Rendering::TextStyle style{text_color, tokens.font_small, 500};
    renderer.DrawText(
        text_,
        CenteredTextRect(renderer, text_, style, tokens.spacing_small),
        style
    );
}

void Badge::Layout(const Layout::Rect& bounds) {
    SetBounds(bounds);
}

bool Badge::HandleEvent(const Event&) {
    return false;
}

Layout::Size Badge::GetMinSize() const {
    return {0.0f, 20.0f};
}

Layout::Size Badge::GetPreferredSize() const {
    return {
        static_cast<float>(Utf8Length(text_)) * 6.6f + 16.0f,
        12.0f + 8.0f
    };
}

Layout::Size Badge::GetMaxSize() const {
    return {100000.0f, 100000.0f};
}

} // namespace XER::UI::Widgets
