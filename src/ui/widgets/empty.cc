#include "empty.h"

#include <algorithm>
#include <utility>

#include "rendering/renderer.h"
#include "theme/tokens.h"

namespace XER::UI::Widgets {

Empty::Empty() {
    SetFocusable(false);
}

void Empty::SetTitle(std::string text) {
    if (title_ == text) return;
    title_ = std::move(text);
    Invalidate();
    RequestLayout();
}

void Empty::SetDescription(std::string text) {
    if (description_ == text) return;
    description_ = std::move(text);
    Invalidate();
    RequestLayout();
}

void Empty::Render(Rendering::Renderer& renderer,
                   const Theme::Tokens& tokens) {
    const auto bounds = GetBounds();
    const auto& children = GetChildren();

    float y = bounds.y + tokens.spacing_large;

    // First child = icon slot, rendered by the tree; we only position text.
    if (!children.empty()) {
        y += 56.0f + tokens.spacing_medium;
    }

    if (!title_.empty()) {
        const Rendering::TextStyle style{tokens.foreground, tokens.font_title, 600};
        const auto size = renderer.MeasureText(title_, style);
        Layout::Rect rect;
        rect.x = bounds.x + (bounds.width - size.width) * 0.5f;
        rect.y = y;
        rect.width = size.width;
        rect.height = size.height;
        renderer.DrawText(title_, rect, style);
        y += size.height + tokens.spacing_small;
    }
    if (!description_.empty()) {
        const Rendering::TextStyle style{tokens.muted, tokens.font_body, 400};
        const auto size = renderer.MeasureText(description_, style);
        Layout::Rect rect;
        rect.x = bounds.x + (bounds.width - size.width) * 0.5f;
        rect.y = y;
        rect.width = size.width;
        rect.height = size.height;
        renderer.DrawText(description_, rect, style);
    }
}

void Empty::Layout(const Layout::Rect& bounds) {
    SetBounds(bounds);
    const auto& children = GetChildren();
    if (children.empty()) return;

    const auto& tokens = CachedLayoutTokens();
    const std::size_t action_begin = !children.empty() ? 1 : 0;

    // Icon slot: first child centered near the top.
    if (!children.empty()) {
        Layout::Rect icon{bounds.x + bounds.width * 0.5f - 24.0f, bounds.y,
                          48.0f, 48.0f};
        children[0]->Layout(icon, tokens);
    }

    // Action children: centered row near the bottom.
    if (children.size() > action_begin) {
        float total_w = 0.0f;
        float max_h = 0.0f;
        for (std::size_t i = action_begin; i < children.size(); ++i) {
            const auto s = children[i]->GetPreferredSize();
            total_w += s.width + (i > action_begin ? tokens.spacing_medium : 0.0f);
            max_h = std::max(max_h, s.height);
        }
        float x = bounds.x + (bounds.width - total_w) * 0.5f;
        const float y = bounds.y + bounds.height - max_h - tokens.spacing_large;
        for (std::size_t i = action_begin; i < children.size(); ++i) {
            const auto s = children[i]->GetPreferredSize();
            Layout::Rect rect{x, y, s.width, max_h};
            children[i]->Layout(rect, tokens);
            x += s.width + tokens.spacing_medium;
        }
    }
}

bool Empty::HandleEvent(const Event&) {
    return false;
}

Layout::Size Empty::GetMinSize() const {
    return {0.0f, 0.0f};
}

Layout::Size Empty::GetPreferredSize() const {
    float height = 0.0f;
    if (!GetChildren().empty()) height += 104.0f;
    if (!title_.empty()) height += 34.0f;
    if (!description_.empty()) height += 26.0f;
    if (GetChildren().size() > 1) height += 48.0f;
    return {0.0f, std::max(height, 96.0f)};
}

Layout::Size Empty::GetMaxSize() const {
    return {100000.0f, 100000.0f};
}

} // namespace XER::UI::Widgets
