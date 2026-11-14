#include "kbd.h"

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

Kbd::Kbd(std::string text) : text_(std::move(text)) {
    SetFocusable(false);
}

void Kbd::SetText(std::string text) {
    if (text_ == text) return;
    text_ = std::move(text);
    Invalidate();
    RequestLayout();
}

void Kbd::Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) {
    const auto bounds = GetBounds();
    renderer.FillRect(bounds, tokens.surface, tokens.radius_small);
    renderer.StrokeRect(bounds, tokens.border, 1.0f, tokens.radius_small);
    const Rendering::TextStyle style{tokens.foreground, tokens.font_small, 500};
    const auto text_size = renderer.MeasureText(text_, style);
    Layout::Rect text_rect;
    text_rect.x = bounds.x + (bounds.width - text_size.width) * 0.5f;
    text_rect.y = bounds.y + (bounds.height - text_size.height) * 0.5f;
    text_rect.width = text_size.width;
    text_rect.height = text_size.height;
    renderer.DrawText(text_, text_rect, style);
}

void Kbd::Layout(const Layout::Rect& bounds) {
    SetBounds(bounds);
}

bool Kbd::HandleEvent(const Event&) {
    return false;
}

Layout::Size Kbd::GetMinSize() const {
    return {24.0f, 20.0f};
}

Layout::Size Kbd::GetPreferredSize() const {
    return {static_cast<float>(Utf8Length(text_)) * 7.0f + 12.0f, 22.0f};
}

Layout::Size Kbd::GetMaxSize() const {
    return {100000.0f, 100000.0f};
}

} // namespace XER::UI::Widgets
