#include "field.h"

#include <algorithm>
#include <utility>

#include "rendering/renderer.h"
#include "theme/tokens.h"

namespace XER::UI::Widgets {

Field::Field() {
    SetFocusable(false);
}

void Field::SetLabel(std::string text) {
    if (label_ == text) return;
    label_ = std::move(text);
    Invalidate();
    RequestLayout();
}

void Field::SetDescription(std::string text) {
    if (description_ == text) return;
    description_ = std::move(text);
    Invalidate();
    RequestLayout();
}

void Field::SetError(std::string text) {
    if (error_ == text) return;
    error_ = std::move(text);
    Invalidate();
    RequestLayout();
}

void Field::Render(Rendering::Renderer& renderer,
                   const Theme::Tokens& tokens) {
    const auto bounds = GetBounds();
    const float label_h = tokens.font_small * 1.4f;
    const float footer_h = tokens.font_small * 1.5f;
    float y = bounds.y;

    if (!label_.empty()) {
        const Rendering::TextStyle style{tokens.foreground, tokens.font_small, 500};
        renderer.DrawText(label_, {bounds.x, y, bounds.width, label_h}, style);
        y += label_h + tokens.spacing_small * 0.5f;
    }

    const std::string& footer = !error_.empty() ? error_ : description_;
    if (!footer.empty()) {
        const Rendering::TextStyle style{
            !error_.empty() ? tokens.danger : tokens.muted,
            tokens.font_small, 400};
        renderer.DrawText(footer,
                          {bounds.x, bounds.y + bounds.height - footer_h,
                           bounds.width, footer_h},
                          style);
    }
}

void Field::Layout(const Layout::Rect& bounds) {
    SetBounds(bounds);
    const auto& children = GetChildren();
    if (children.empty()) return;

    const auto& tokens = CachedLayoutTokens();
    const float label_h = tokens.font_small * 1.4f + tokens.spacing_small * 0.5f;
    const float footer_h = tokens.font_small * 1.5f + 6.0f;
    const bool has_footer = !error_.empty() || !description_.empty();

    Layout::Rect inner;
    inner.x = bounds.x;
    inner.y = bounds.y + (!label_.empty() ? label_h : 0.0f);
    inner.width = bounds.width;
    inner.height = std::max(
        0.0f, bounds.height - (!label_.empty() ? label_h : 0.0f) -
                   (has_footer ? footer_h : 0.0f));
    for (const auto& child : children) {
        child->Layout(inner, tokens);
    }
}

bool Field::HandleEvent(const Event&) {
    return false;
}

Layout::Size Field::GetMinSize() const {
    return {0.0f, 0.0f};
}

Layout::Size Field::GetPreferredSize() const {
    float height = 0.0f;
    if (!label_.empty()) height += 22.0f;
    for (const auto& child : GetChildren()) {
        height += child->GetPreferredSize().height;
    }
    if (!error_.empty() || !description_.empty()) height += 22.0f;
    return {0.0f, height};
}

Layout::Size Field::GetMaxSize() const {
    return {100000.0f, 100000.0f};
}

} // namespace XER::UI::Widgets
