#include "badge.h"

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

Badge::Badge(std::string text) : text_(std::move(text)) {
    SetFocusable(false);
}

void Badge::SetText(std::string text) {
    text_ = std::move(text);
    Invalidate();
    RequestLayout();
}

const std::string& Badge::GetText() const {
    return text_;
}

void Badge::Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) {
    const auto bounds = GetBounds();
    renderer.FillRect(bounds, IsEnabled() ? tokens.accent : tokens.muted, tokens.radius_small);
    renderer.StrokeRect(bounds, tokens.border, 1.0f, tokens.radius_small);
    renderer.DrawText(
        text_,
        Layout::EdgeInsets(tokens.spacing_medium, tokens.spacing_small).Apply(bounds),
        Rendering::TextStyle{
            IsEnabled() ? tokens.background : tokens.foreground,
            tokens.font_small,
            true
        }
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
