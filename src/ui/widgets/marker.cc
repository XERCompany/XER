#include "marker.h"

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

Marker::Marker(std::string text) : text_(std::move(text)) {
    SetFocusable(false);
}

void Marker::SetText(std::string text) {
    if (text_ == text) return;
    text_ = std::move(text);
    Invalidate();
    RequestLayout();
}

void Marker::Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) {
    const auto bounds = GetBounds();
    renderer.FillRect(bounds, tokens.hover, tokens.radius_small);
    const Rendering::TextStyle style{tokens.foreground, tokens.font_body, 400};
    renderer.DrawText(text_, CenteredTextRect(renderer, text_, style, 4.0f), style);
}

void Marker::Layout(const Layout::Rect& bounds) {
    SetBounds(bounds);
}

bool Marker::HandleEvent(const Event&) {
    return false;
}

Layout::Size Marker::GetMinSize() const {
    return {0.0f, 0.0f};
}

Layout::Size Marker::GetPreferredSize() const {
    return {static_cast<float>(Utf8Length(text_)) * 7.7f + 8.0f, 20.0f};
}

Layout::Size Marker::GetMaxSize() const {
    return {100000.0f, 100000.0f};
}

} // namespace XER::UI::Widgets
