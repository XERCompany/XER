#include "card.h"

#include <algorithm>

#include "rendering/renderer.h"
#include "theme/tokens.h"

namespace XER::UI::Widgets {
namespace {

constexpr float kPadding = 16.0f;
constexpr float kMinimumDimension = 32.0f;

} // namespace

void Card::Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) {
    const auto bounds = GetBounds();
    renderer.FillRect(bounds, tokens.surface, tokens.radius_medium);
    renderer.StrokeRect(bounds, tokens.border, 1.0f, tokens.radius_medium);
}

void Card::Layout(const Layout::Rect& bounds) {
    SetBounds(bounds);
    const auto content_bounds = Layout::EdgeInsets(kPadding).Apply(bounds);
    for (const auto& child : GetChildren()) {
        child->Layout(content_bounds);
    }
}

void Card::Layout(const Layout::Rect& bounds, const Theme::Tokens& tokens) {
    SetBounds(bounds);
    const auto content_bounds = Layout::EdgeInsets(tokens.spacing_large).Apply(bounds);
    for (const auto& child : GetChildren()) {
        child->Layout(content_bounds, tokens);
    }
}

bool Card::HandleEvent(const Event&) {
    return false;
}

Layout::Size Card::GetMinSize() const {
    Layout::Size child_size;
    for (const auto& child : GetChildren()) {
        const auto size = child->GetMinSize();
        child_size.width = std::max(child_size.width, size.width);
        child_size.height = std::max(child_size.height, size.height);
    }
    return {
        std::max(kMinimumDimension, child_size.width + 2.0f * kPadding),
        std::max(kMinimumDimension, child_size.height + 2.0f * kPadding)
    };
}

Layout::Size Card::GetPreferredSize() const {
    Layout::Size child_size;
    for (const auto& child : GetChildren()) {
        const auto size = child->GetPreferredSize();
        child_size.width = std::max(child_size.width, size.width);
        child_size.height = std::max(child_size.height, size.height);
    }
    return {
        std::max(kMinimumDimension, child_size.width + 2.0f * kPadding),
        std::max(kMinimumDimension, child_size.height + 2.0f * kPadding)
    };
}

Layout::Size Card::GetMaxSize() const {
    return {100000.0f, 100000.0f};
}

} // namespace XER::UI::Widgets
