#include "bubble.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "rendering/renderer.h"
#include "theme/tokens.h"

namespace XER::UI::Widgets {
namespace {

std::size_t Utf8Length(std::string_view text) {
    std::size_t count = 0;
    for (const unsigned char byte : text) {
        if ((byte & 0xC0u) != 0x80u) ++count;
    }
    return count;
}

} // namespace

Bubble::Bubble(std::string text, Align align)
    : text_(std::move(text)), align_(align) {
    SetFocusable(false);
}

void Bubble::SetText(std::string text) {
    if (text_ == text) return;
    text_ = std::move(text);
    Invalidate();
    RequestLayout();
}

void Bubble::SetAlign(Align align) {
    if (align_ == align) return;
    align_ = align;
    Invalidate();
}

void Bubble::Render(Rendering::Renderer& renderer,
                    const Theme::Tokens& tokens) {
    const auto bounds = GetBounds();
    if (bounds.width <= 0.0f || bounds.height <= 0.0f) return;

    const bool own = align_ == Align::Own;
    const Theme::Color fill = own ? tokens.accent : tokens.surface;
    const Theme::Color stroke = own ? tokens.accent : tokens.border;
    const Theme::Color text_color =
        own ? tokens.accent_foreground : tokens.foreground;

    const float radius = tokens.radius_large;
    renderer.FillRect(bounds, fill, radius);
    renderer.StrokeRect(bounds, stroke, 1.0f, radius);

    // Tail: small triangle-ish notch on the aligned bottom corner, drawn
    // as a tiny filled square rotated is overkill — a short diagonal line
    // pair reads as a tail at UI scale.
    const float tail = 8.0f;
    const float ty = bounds.y + bounds.height - tail * 0.5f;
    if (own) {
        renderer.DrawLine({bounds.x + bounds.width - tail, ty},
                          {bounds.x + bounds.width - 2.0f, bounds.y + bounds.height},
                          fill, 2.0f);
    } else {
        renderer.DrawLine({bounds.x + 2.0f, bounds.y + bounds.height},
                          {bounds.x + tail, ty}, fill, 2.0f);
    }

    const float pad_x = 12.0f;
    const float pad_y = 8.0f;
    const Rendering::TextStyle style{text_color, tokens.font_body, 400};
    Layout::Rect text_rect;
    text_rect.x = bounds.x + pad_x;
    text_rect.y = bounds.y + pad_y;
    text_rect.width = std::max(0.0f, bounds.width - pad_x * 2.0f);
    text_rect.height = std::max(0.0f, bounds.height - pad_y * 2.0f);
    renderer.DrawText(text_, text_rect, style);
}

void Bubble::Layout(const Layout::Rect& bounds) {
    SetBounds(bounds);
}

Layout::Size Bubble::GetMinSize() const {
    return {60.0f, 30.0f};
}

Layout::Size Bubble::GetPreferredSize() const {
    // Cap bubble width at ~320px so long messages wrap instead of
    // stretching edge to edge. ~7px per glyph at font_body approximates
    // the text measure (no renderer available at layout time).
    const float max_w = 320.0f;
    const float char_w = 7.0f;
    const float text_w = static_cast<float>(Utf8Length(text_)) * char_w;
    const float inner_w = std::max(1.0f, text_w);
    const float w = std::min(max_w, inner_w + 24.0f);
    const int lines =
        std::max(1, static_cast<int>(std::ceil(inner_w / (w - 24.0f))));
    const float h = 16.0f + static_cast<float>(lines) * 18.0f;
    return {std::max(60.0f, w), std::max(30.0f, h)};
}

Layout::Size Bubble::GetMaxSize() const {
    return {420.0f, 100000.0f};
}

} // namespace XER::UI::Widgets
