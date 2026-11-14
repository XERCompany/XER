#include "direction.h"

namespace XER::UI::Widgets {

Direction::Direction(TextDirection direction) : direction_(direction) {
    SetFocusable(false);
    SetTextDirection(direction);
}

void Direction::SetDirection(TextDirection direction) {
    direction_ = direction;
    SetTextDirection(direction);
    Invalidate();
    RequestLayout();
}

void Direction::Layout(const Layout::Rect& bounds) {
    SetBounds(bounds);
    for (const auto& child : GetChildren()) {
        child->Layout(bounds, CachedLayoutTokens());
    }
}

Layout::Size Direction::GetMinSize() const {
    Layout::Size size{0.0f, 0.0f};
    for (const auto& child : GetChildren()) {
        const auto s = child->GetMinSize();
        size.width = std::max(size.width, s.width);
        size.height = std::max(size.height, s.height);
    }
    return size;
}

Layout::Size Direction::GetPreferredSize() const {
    Layout::Size size{0.0f, 0.0f};
    for (const auto& child : GetChildren()) {
        const auto s = child->GetPreferredSize();
        size.width = std::max(size.width, s.width);
        size.height = std::max(size.height, s.height);
    }
    return size;
}

Layout::Size Direction::GetMaxSize() const {
    return {100000.0f, 100000.0f};
}

} // namespace XER::UI::Widgets
