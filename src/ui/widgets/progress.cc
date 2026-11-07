#include "progress.h"

#include <algorithm>

#include "motion/motion.h"
#include "rendering/renderer.h"
#include "theme/tokens.h"

namespace XER::UI::Widgets {

Progress::Progress(float value) : value_(std::clamp(value, 0.0f, 1.0f)) {
    SetFocusable(false);
    shown_.Set(value_);
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

void Progress::Tick(double dt_seconds) {
    if (!IsEffectivelyVisible()) {
        return;
    }
    if (shown_.to != value_) {
        shown_.Set(value_, 0.25f);
    }
    shown_.Update(static_cast<float>(dt_seconds));
    if (shown_.Active()) {
        Invalidate();
    }
}

void Progress::Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) {
    const auto bounds = GetBounds();
    const float radius = bounds.height / 2.0f;
    const float shown = shown_.Value();
    renderer.FillRect(bounds, tokens.secondary, radius);
    if (shown > 0.0f) {
        renderer.FillRect(
            {bounds.x, bounds.y, bounds.width * shown, bounds.height},
            IsEnabled() ? tokens.foreground : tokens.muted,
            radius
        );
    }
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
