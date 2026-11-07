#include "skeleton.h"

#include <cmath>

#include "motion/motion.h"
#include "rendering/renderer.h"
#include "theme/tokens.h"

namespace XER::UI::Widgets {

Skeleton::Skeleton(float width, float height, Variant variant)
    : width_(width)
    , height_(height)
    , variant_(variant) {
}

void Skeleton::SetSize(float width, float height) {
    if (width_ == width && height_ == height) {
        return;
    }
    width_ = width;
    height_ = height;
    Invalidate();
    RequestLayout();
}

void Skeleton::SetVariant(Variant variant) {
    if (variant_ == variant) {
        return;
    }
    variant_ = variant;
    Invalidate();
}

void Skeleton::Tick(double dt_seconds) {
    if (!IsEffectivelyVisible() || Motion::ReducedMotion()) {
        return;
    }
    shimmer_t_ += static_cast<float>(dt_seconds);
    Invalidate();
}

void Skeleton::Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) {
    const auto bounds = GetBounds();
    const float radius = variant_ == Variant::Text ? bounds.height * 0.5f : tokens.radius_small;
    renderer.FillRect(bounds, tokens.secondary, radius);
    // Shimmer band sweeping left to right, ~1.8s per pass.
    const float band_width = bounds.width * 0.4f;
    if (band_width > 0.0f && bounds.width > 0.0f) {
        const float speed = (bounds.width + band_width) / 1.8f;
        const float offset = static_cast<float>(std::fmod(
            shimmer_t_ * speed,
            static_cast<double>(bounds.width + band_width))) - band_width;
        renderer.FillRect(
            Layout::Rect{bounds.x + offset, bounds.y, band_width, bounds.height},
            tokens.hover,
            radius
        );
    }
}

void Skeleton::Layout(const Layout::Rect& bounds) {
    SetBounds(bounds);
}

bool Skeleton::HandleEvent(const Event&) {
    return false;
}

Layout::Size Skeleton::GetMinSize() const {
    return {width_, height_};
}

Layout::Size Skeleton::GetPreferredSize() const {
    return {width_ > 0.0f ? width_ : 200.0f, height_ > 0.0f ? height_ : 16.0f};
}

Layout::Size Skeleton::GetMaxSize() const {
    return {100000.0f, 100000.0f};
}

} // namespace XER::UI::Widgets
