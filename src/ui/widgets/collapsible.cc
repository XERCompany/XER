#include "collapsible.h"

#include <algorithm>
#include <utility>

#include "rendering/renderer.h"
#include "theme/tokens.h"

namespace XER::UI::Widgets {

Collapsible::Collapsible(bool open)
    : open_(open), progress_(open ? 1.0f : 0.0f) {
    SetFocusable(false);
    SetClipsChildren(true);
}

void Collapsible::SetOpen(bool open) {
    if (open_ == open) return;
    open_ = open;
    SetVisible(true);
    if (on_toggled_) on_toggled_(open);
    Invalidate();
}

void Collapsible::SetOnToggled(std::function<void(bool)> callback) {
    on_toggled_ = std::move(callback);
}

void Collapsible::Tick(double dt_seconds) {
    const float target = open_ ? 1.0f : 0.0f;
    if (progress_ == target) return;
    const float step = static_cast<float>(dt_seconds) * 6.0f;
    progress_ = std::clamp(progress_ + (open_ ? step : -step), 0.0f, 1.0f);
    if (progress_ == 0.0f && !open_) SetVisible(false);
    Invalidate();
}

void Collapsible::Render(Rendering::Renderer&, const Theme::Tokens&) {
}

void Collapsible::Layout(const Layout::Rect& bounds) {
    SetBounds(bounds);
    const auto& children = GetChildren();
    if (children.empty()) {
        content_height_ = 0.0f;
        return;
    }

    // Measure content height once from children's preferred heights.
    float content_h = 0.0f;
    for (const auto& child : children) {
        content_h = std::max(content_h, child->GetPreferredSize().height);
    }
    content_height_ = content_h;

    const auto& tokens = CachedLayoutTokens();
    Layout::Rect inner = bounds;
    inner.height = content_height_;
    for (const auto& child : children) {
        child->Layout(inner, tokens);
    }
}

bool Collapsible::HandleEvent(const Event&) {
    return false;
}

Layout::Size Collapsible::GetMinSize() const {
    return {0.0f, 0.0f};
}

Layout::Size Collapsible::GetPreferredSize() const {
    return {0.0f, content_height_ * progress_};
}

Layout::Size Collapsible::GetMaxSize() const {
    return {100000.0f, content_height_};
}

} // namespace XER::UI::Widgets
