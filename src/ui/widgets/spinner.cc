#include "spinner.h"

#include <cmath>

#include "motion/motion.h"
#include "rendering/renderer.h"
#include "theme/tokens.h"

namespace XER::UI::Widgets {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr int kSpokeCount = 8;

} // namespace

Spinner::Spinner(float phase) : phase_(phase) {
    SetFocusable(false);
}

void Spinner::SetPhase(float phase) {
    if (phase_ == phase) {
        return;
    }
    phase_ = phase;
    Invalidate();
}

void Spinner::Tick(double dt_seconds) {
    if (!IsEffectivelyVisible() || Motion::ReducedMotion()) {
        return;
    }
    // One revolution per ~1.1s; the spoke trail does the easing visually.
    phase_ = std::fmod(phase_ + static_cast<float>(dt_seconds) / 1.1f, 1.0f);
    Invalidate();
}

void Spinner::Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) {
    const auto bounds = GetBounds();
    const float center_x = bounds.x + bounds.width / 2.0f;
    const float center_y = bounds.y + bounds.height / 2.0f;
    const float radius = (bounds.width < bounds.height ? bounds.width : bounds.height) * 0.38f;
    const float inner_radius = radius * 0.45f;
    const float angle_offset = phase_ * 2.0f * kPi;

    for (int spoke = 0; spoke < kSpokeCount; ++spoke) {
        const float angle = angle_offset + 2.0f * kPi * static_cast<float>(spoke) /
            static_cast<float>(kSpokeCount);
        const Layout::Point start{
            center_x + std::cos(angle) * inner_radius,
            center_y + std::sin(angle) * inner_radius
        };
        const Layout::Point end{
            center_x + std::cos(angle) * radius,
            center_y + std::sin(angle) * radius
        };
        // Fade the trail behind the leading spoke, like shadcn's Loader2.
        Theme::Color spoke_color = tokens.foreground;
        spoke_color.a = 0.15f + 0.85f * static_cast<float>(spoke) /
            static_cast<float>(kSpokeCount - 1);
        renderer.DrawLine(start, end, spoke_color, 2.0f);
    }
}

void Spinner::Layout(const Layout::Rect& bounds) {
    SetBounds(bounds);
}

bool Spinner::HandleEvent(const Event&) {
    return false;
}

Layout::Size Spinner::GetMinSize() const {
    return {16.0f, 16.0f};
}

Layout::Size Spinner::GetPreferredSize() const {
    return {24.0f, 24.0f};
}

Layout::Size Spinner::GetMaxSize() const {
    return {100000.0f, 100000.0f};
}

} // namespace XER::UI::Widgets
