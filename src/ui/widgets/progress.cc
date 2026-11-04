#include "progress.h"

#include <algorithm>

#include "rendering/renderer.h"
#include "theme/tokens.h"

namespace XER::UI::Widgets {

Progress::Progress(float value) : value_(std::clamp(value, 0.0f, 1.0f)) {
    SetFocusable(false);
}

void Progress::SetValue(float value) {
    const float clamped = std::clamp(value, 0.0f, 1.0f);
    if (clamped == value_) {
        return;
    }
    value_ = clamped;
    Invalidate();
}

float Progress::GetValue() const {
    return value_;
}

void Progress::Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) {
    const auto bounds = GetBounds();
    renderer.FillRect(bounds, tokens.muted, tokens.radius_small);
    renderer.FillRect(
        {bounds.x, bounds.y, bounds.width * value_, bounds.height},
        tokens.accent,
        tokens.radius_small
    );
}

void Progress::Layout(const Layout::Rect& bounds) {
    SetBounds(bounds);
}

bool Progress::HandleEvent(const Event&) {
    return false;
}

Layout::Size Progress::GetMinSize() const {
    return {0.0f, 4.0f};
}

Layout::Size Progress::GetPreferredSize() const {
    return {120.0f, 8.0f};
}

Layout::Size Progress::GetMaxSize() const {
    return {100000.0f, 100000.0f};
}

} // namespace XER::UI::Widgets
