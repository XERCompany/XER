#include "button_group.h"

#include <algorithm>

#include "rendering/renderer.h"
#include "theme/tokens.h"

namespace XER::UI::Widgets {

ButtonGroup::ButtonGroup(Orientation orientation)
    : orientation_(orientation) {
    SetFocusable(false);
}

void ButtonGroup::SetOrientation(Orientation orientation) {
    if (orientation_ == orientation) return;
    orientation_ = orientation;
    Invalidate();
    RequestLayout();
}

std::vector<Layout::Rect> ButtonGroup::ChildRects(
    const Layout::Rect& bounds) const {
    const auto& children = GetChildren();
    std::vector<Layout::Rect> rects;
    rects.reserve(children.size());
    if (children.empty()) return rects;

    const Layout::Rect inner = bounds;
    const bool horizontal = orientation_ == Orientation::Horizontal;
    const float total = horizontal ? inner.width : inner.height;
    const float each = total / static_cast<float>(children.size());
    for (std::size_t i = 0; i < children.size(); ++i) {
        Layout::Rect r = inner;
        if (horizontal) {
            r.x = inner.x + each * static_cast<float>(i);
            r.width = each;
        } else {
            r.y = inner.y + each * static_cast<float>(i);
            r.height = each;
        }
        rects.push_back(r);
    }
    return rects;
}

void ButtonGroup::Render(Rendering::Renderer& renderer,
                         const Theme::Tokens& tokens) {
    const auto bounds = GetBounds();
    renderer.FillRect(bounds, tokens.surface, tokens.radius_medium);
    renderer.StrokeRect(bounds, tokens.border, 1.0f, tokens.radius_medium);

    const auto rects = ChildRects(bounds);
    const bool horizontal = orientation_ == Orientation::Horizontal;
    for (std::size_t i = 1; i < rects.size(); ++i) {
        if (horizontal) {
            const float x = rects[i].x;
            renderer.DrawLine({x, bounds.y + 4.0f},
                              {x, bounds.y + bounds.height - 4.0f},
                              tokens.border, 1.0f);
        } else {
            const float y = rects[i].y;
            renderer.DrawLine({bounds.x + 4.0f, y},
                              {bounds.x + bounds.width - 4.0f, y},
                              tokens.border, 1.0f);
        }
    }
}

void ButtonGroup::Layout(const Layout::Rect& bounds) {
    SetBounds(bounds);
    const auto rects = ChildRects(bounds);
    const auto& children = GetChildren();
    for (std::size_t i = 0; i < children.size() && i < rects.size(); ++i) {
        children[i]->Layout(rects[i], CachedLayoutTokens());
    }
}

bool ButtonGroup::HandleEvent(const Event&) {
    return false;
}

Layout::Size ButtonGroup::GetMinSize() const {
    return {0.0f, 0.0f};
}

Layout::Size ButtonGroup::GetPreferredSize() const {
    float width = 0.0f;
    float height = 0.0f;
    for (const auto& child : GetChildren()) {
        const auto size = child->GetPreferredSize();
        if (orientation_ == Orientation::Horizontal) {
            width += size.width;
            height = std::max(height, size.height);
        } else {
            width = std::max(width, size.width);
            height += size.height;
        }
    }
    return {width, height};
}

Layout::Size ButtonGroup::GetMaxSize() const {
    return {100000.0f, 100000.0f};
}

} // namespace XER::UI::Widgets
