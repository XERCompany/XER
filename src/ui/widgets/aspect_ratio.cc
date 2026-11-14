#include "aspect_ratio.h"

#include "rendering/renderer.h"
#include "theme/tokens.h"

namespace XER::UI::Widgets {

AspectRatio::AspectRatio(float ratio) : ratio_(ratio > 0.0f ? ratio : 1.0f) {
    SetFocusable(false);
}

void AspectRatio::SetRatio(float ratio) {
    if (ratio <= 0.0f || ratio_ == ratio) return;
    ratio_ = ratio;
    Invalidate();
    RequestLayout();
}

Layout::Rect AspectRatio::ContentRect(const Layout::Rect& bounds) const {
    const float bounds_ratio = bounds.width / std::max(bounds.height, 1.0f);
    Layout::Rect rect = bounds;
    if (bounds_ratio > ratio_) {
        const float width = bounds.height * ratio_;
        rect.x = bounds.x + (bounds.width - width) * 0.5f;
        rect.width = width;
    } else {
        const float height = bounds.width / ratio_;
        rect.y = bounds.y + (bounds.height - height) * 0.5f;
        rect.height = height;
    }
    return rect;
}

void AspectRatio::Render(Rendering::Renderer&, const Theme::Tokens&) {
}

void AspectRatio::Layout(const Layout::Rect& bounds) {
    SetBounds(bounds);
    const auto content = ContentRect(bounds);
    for (const auto& child : GetChildren()) {
        child->Layout(content, CachedLayoutTokens());
    }
}

bool AspectRatio::HandleEvent(const Event&) {
    return false;
}

Layout::Size AspectRatio::GetMinSize() const {
    return {0.0f, 0.0f};
}

Layout::Size AspectRatio::GetPreferredSize() const {
    return {320.0f, 320.0f / ratio_};
}

Layout::Size AspectRatio::GetMaxSize() const {
    return {100000.0f, 100000.0f};
}

} // namespace XER::UI::Widgets
