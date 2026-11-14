#include "input_group.h"

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

InputGroup::InputGroup() {
    SetFocusable(false);
}

void InputGroup::SetPrefix(std::string text) {
    if (prefix_ == text) return;
    prefix_ = std::move(text);
    Invalidate();
    RequestLayout();
}

void InputGroup::SetSuffix(std::string text) {
    if (suffix_ == text) return;
    suffix_ = std::move(text);
    Invalidate();
    RequestLayout();
}

void InputGroup::Render(Rendering::Renderer& renderer,
                        const Theme::Tokens& tokens) {
    const auto bounds = GetBounds();
    renderer.FillRect(bounds, tokens.surface, tokens.radius_medium);
    renderer.StrokeRect(bounds, tokens.border, 1.0f, tokens.radius_medium);

    const Rendering::TextStyle style{tokens.muted, tokens.font_body, 400};
    const float pad = tokens.spacing_medium;
    if (!prefix_.empty()) {
        renderer.DrawText(prefix_,
                          {bounds.x + pad, bounds.y,
                           static_cast<float>(Utf8Length(prefix_)) * 7.7f,
                           bounds.height},
                          style);
    }
    if (!suffix_.empty()) {
        const float w = static_cast<float>(Utf8Length(suffix_)) * 7.7f;
        renderer.DrawText(suffix_,
                          {bounds.x + bounds.width - pad - w, bounds.y, w,
                           bounds.height},
                          style);
    }
}

void InputGroup::Layout(const Layout::Rect& bounds) {
    SetBounds(bounds);
    const float pad = 8.0f;
    float left = bounds.x + pad;
    float right = bounds.x + bounds.width - pad;
    if (!prefix_.empty()) {
        left += static_cast<float>(Utf8Length(prefix_)) * 7.7f + 8.0f;
    }
    if (!suffix_.empty()) {
        right -= static_cast<float>(Utf8Length(suffix_)) * 7.7f + 8.0f;
    }
    const float vpad = 4.0f;
    Layout::Rect inner;
    inner.x = left;
    inner.y = bounds.y + vpad;
    inner.width = std::max(0.0f, right - left);
    inner.height = std::max(0.0f, bounds.height - 2.0f * vpad);
    for (const auto& child : GetChildren()) {
        child->Layout(inner, CachedLayoutTokens());
    }
}


bool InputGroup::HandleEvent(const Event&) {
    return false;
}

Layout::Size InputGroup::GetMinSize() const {
    return {80.0f, 28.0f};
}

Layout::Size InputGroup::GetPreferredSize() const {
    float addons = 0.0f;
    if (!prefix_.empty())
        addons += static_cast<float>(Utf8Length(prefix_)) * 7.7f + 12.0f;
    if (!suffix_.empty())
        addons += static_cast<float>(Utf8Length(suffix_)) * 7.7f + 12.0f;
    float child_width = 0.0f;
    float child_height = 0.0f;
    for (const auto& child : GetChildren()) {
        const auto size = child->GetPreferredSize();
        child_width = std::max(child_width, size.width);
        child_height = std::max(child_height, size.height);
    }
    return {child_width + addons + 16.0f,
            std::max(child_height + 8.0f, 32.0f)};
}

Layout::Size InputGroup::GetMaxSize() const {
    return {100000.0f, 100000.0f};
}

} // namespace XER::UI::Widgets
