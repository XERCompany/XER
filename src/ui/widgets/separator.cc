#include "separator.h"

#include "rendering/renderer.h"
#include "theme/tokens.h"

namespace XER::UI::Widgets {

Separator::Separator(Orientation orientation) : orientation_(orientation) {
    SetFocusable(false);
}

void Separator::Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) {
    const auto bounds = GetBounds();
    if (orientation_ == Orientation::Horizontal) {
        const float y = bounds.y + bounds.height / 2.0f;
        renderer.DrawLine(
            {bounds.x, y},
            {bounds.x + bounds.width, y},
            tokens.border,
            1.0f
        );
        return;
    }

    const float x = bounds.x + bounds.width / 2.0f;
    renderer.DrawLine(
        {x, bounds.y},
        {x, bounds.y + bounds.height},
        tokens.border,
        1.0f
    );
}

void Separator::Layout(const Layout::Rect& bounds) {
    SetBounds(bounds);
}

bool Separator::HandleEvent(const Event&) {
    return false;
}

Layout::Size Separator::GetMinSize() const {
    return orientation_ == Orientation::Horizontal
        ? Layout::Size{0.0f, 1.0f}
        : Layout::Size{1.0f, 0.0f};
}

Layout::Size Separator::GetPreferredSize() const {
    return GetMinSize();
}

Layout::Size Separator::GetMaxSize() const {
    return {100000.0f, 100000.0f};
}

} // namespace XER::UI::Widgets
