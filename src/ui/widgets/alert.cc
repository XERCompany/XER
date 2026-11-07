#include "alert.h"

#include <utility>

#include "rendering/renderer.h"
#include "theme/tokens.h"

namespace XER::UI::Widgets {

Alert::Alert(std::string title, std::string description, Variant variant)
    : title_(std::move(title))
    , description_(std::move(description))
    , variant_(variant) {
}

void Alert::SetTitle(std::string title) {
    if (title_ == title) {
        return;
    }
    title_ = std::move(title);
    Invalidate();
    RequestLayout();
}

void Alert::SetDescription(std::string description) {
    if (description_ == description) {
        return;
    }
    description_ = std::move(description);
    Invalidate();
    RequestLayout();
}

void Alert::SetVariant(Variant variant) {
    if (variant_ == variant) {
        return;
    }
    variant_ = variant;
    Invalidate();
}

void Alert::Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) {
    const auto bounds = GetBounds();
    const bool destructive = variant_ == Variant::Destructive;

    const Theme::Color bg = destructive ? tokens.surface : tokens.surface;
    const Theme::Color border_color = destructive ? tokens.accent : tokens.border;
    const Theme::Color title_color = destructive ? tokens.accent : tokens.foreground;

    renderer.FillRect(bounds, bg, tokens.radius_medium);
    renderer.StrokeRect(bounds, border_color, 1.0f, tokens.radius_medium);

    // Status glyph: ! for destructive, i for default.
    const Rendering::TextStyle glyph_style{border_color, tokens.font_body, 700};
    const std::string glyph = destructive ? "!" : "i";
    const float glyph_h = renderer.MeasureText(glyph, glyph_style).height;
    renderer.DrawText(
        glyph,
        Layout::Rect{
            bounds.x + tokens.spacing_medium,
            bounds.y + (bounds.height - glyph_h) * 0.5f,
            glyph_h * 1.5f,
            glyph_h
        },
        glyph_style
    );

    const float text_x = bounds.x + tokens.spacing_medium + glyph_h * 1.5f + tokens.spacing_small;
    const Rendering::TextStyle title_style{title_color, tokens.font_body, 500};
    const float title_h = renderer.MeasureText(title_, title_style).height;
    renderer.DrawText(
        title_,
        Layout::Rect{text_x, bounds.y + tokens.spacing_small, bounds.width - text_x - tokens.spacing_medium, title_h},
        title_style
    );

    if (!description_.empty()) {
        const Rendering::TextStyle desc_style{tokens.muted, tokens.font_small, 400};
        const float desc_h = renderer.MeasureText(description_, desc_style).height;
        renderer.DrawText(
            description_,
            Layout::Rect{
                text_x,
                bounds.y + tokens.spacing_small + title_h + 2.0f,
                bounds.width - text_x - tokens.spacing_medium,
                desc_h
            },
            desc_style
        );
    }
}

void Alert::Layout(const Layout::Rect& bounds) {
    SetBounds(bounds);
}

bool Alert::HandleEvent(const Event&) {
    return false;
}

Layout::Size Alert::GetMinSize() const {
    return {0.0f, 40.0f};
}

Layout::Size Alert::GetPreferredSize() const {
    return {0.0f, description_.empty() ? 40.0f : 56.0f};
}

Layout::Size Alert::GetMaxSize() const {
    return {100000.0f, 100000.0f};
}

} // namespace XER::UI::Widgets
