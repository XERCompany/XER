#include "surface.h"

#include <algorithm>
#include <utility>

#include "rendering/renderer.h"

namespace XER::UI::Widgets {

Surface::Surface() {
    // Custom painters draw edge-to-edge dynamic content — clip children
    // and the callback's output to the adopted bounds like other
    // surface hosts (ScrollArea) do.
    SetClipsChildren(true);
}

void Surface::SetPaintCallback(PaintCallback callback) {
    paint_callback_ = std::move(callback);
    Invalidate();
}

void Surface::SetEventCallback(EventCallback callback) {
    event_callback_ = std::move(callback);
    // A surface that wants input almost always wants keyboard focus too.
    SetFocusable(event_callback_ != nullptr);
}

void Surface::SetPreferredSizeCallback(SizeCallback callback) {
    preferred_size_callback_ = std::move(callback);
    RequestLayout();
}

void Surface::SetMinSize(Layout::Size size) {
    min_size_ = size;
    RequestLayout();
}

void Surface::SetPreferredSize(Layout::Size size) {
    preferred_size_ = size;
    RequestLayout();
}

void Surface::SetMaxSize(Layout::Size size) {
    max_size_ = size;
    RequestLayout();
}

void Surface::SetContentSize(Layout::Size size) {
    if (content_size_.width == size.width &&
        content_size_.height == size.height) {
        return;
    }
    content_size_ = size;
    Invalidate();
}

Layout::Rect Surface::ContentRect() const {
    const Layout::Rect bounds = GetBounds();
    if (hasScaledContents() || content_size_.width <= 0.0f ||
        content_size_.height <= 0.0f || bounds.width <= 0.0f ||
        bounds.height <= 0.0f) {
        return bounds;
    }
    // Aspect-preserving letterbox, centered in the bounds.
    const float content_ratio = content_size_.width / content_size_.height;
    const float bounds_ratio = bounds.width / bounds.height;
    Layout::Rect rect = bounds;
    if (bounds_ratio > content_ratio) {
        rect.width = bounds.height * content_ratio;
        rect.x = bounds.x + (bounds.width - rect.width) * 0.5f;
    } else {
        rect.height = bounds.width / content_ratio;
        rect.y = bounds.y + (bounds.height - rect.height) * 0.5f;
    }
    return rect;
}

void Surface::Render(Rendering::Renderer& renderer,
                     const Theme::Tokens& tokens) {
    if (paint_callback_ == nullptr) {
        return;
    }
    renderer.PushClip(GetBounds());
    paint_callback_(renderer, ContentRect(), tokens);
    renderer.PopClip();
}

void Surface::Layout(const Layout::Rect& bounds) {
    SetBounds(bounds);
}

bool Surface::HandleEvent(const Event& event) {
    if (event_callback_ == nullptr || !IsEnabled()) {
        return false;
    }
    return event_callback_(event);
}

Layout::Size Surface::GetMinSize() const {
    return min_size_;
}

Layout::Size Surface::GetPreferredSize() const {
    if (preferred_size_callback_ != nullptr) {
        const Layout::Size size = preferred_size_callback_();
        if (size.width > 0.0f || size.height > 0.0f) {
            return size;
        }
    }
    return preferred_size_;
}

Layout::Size Surface::GetMaxSize() const {
    return max_size_;
}

} // namespace XER::UI::Widgets
