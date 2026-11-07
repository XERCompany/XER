#include "tooltip.h"

#include <algorithm>

#include "motion/motion.h"
#include "rendering/renderer.h"
#include "theme/tokens.h"

namespace XER::UI::Widgets {
namespace {

constexpr float kPadX = 9.0f;
constexpr float kPadY = 6.0f;
constexpr float kGap = 6.0f;
constexpr float kMaxWidth = 320.0f;

} // namespace

std::shared_ptr<Tooltip> Tooltip::Create(std::string text) {
    return std::shared_ptr<Tooltip>(new Tooltip(std::move(text)));
}

Tooltip::Tooltip(std::string text)
    : text_(std::move(text)) {
    // Quick fade-in; exits are handled by the tree removing the widget.
    open_t_.Set(1.0f, 0.12f);
}

void Tooltip::SetAnchor(Layout::Rect anchor) {
    anchor_ = anchor;
    Invalidate();
}

void Tooltip::Layout(const Layout::Rect& bounds) {
    SetBounds(bounds);
}

void Tooltip::Tick(double dt_seconds) {
    if (!IsEffectivelyVisible()) {
        return;
    }
    open_t_.Update(static_cast<float>(dt_seconds));
    if (open_t_.Active()) {
        Invalidate();
    }
}

void Tooltip::Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) {
    const float p = open_t_.Value();
    if (p <= 0.0f || text_.empty()) {
        return;
    }
    const auto bounds = GetBounds();

    // Inverted shadcn tooltip: foreground surface, background text.
    Theme::Color text_color = tokens.background;
    text_color.a *= p;
    const Rendering::TextStyle style{text_color, tokens.font_small, 500};
    auto text_size = renderer.MeasureText(text_, style);
    const float max_text_w = std::max(40.0f, bounds.width - kPadX * 2.0f - 16.0f);
    const float bubble_w = std::min(kMaxWidth, text_size.width + kPadX * 2.0f);
    const float bubble_h = text_size.height + kPadY * 2.0f;

    float x = anchor_.x + anchor_.width * 0.5f - bubble_w * 0.5f;
    float y = anchor_.y + anchor_.height + kGap;
    if (y + bubble_h > bounds.y + bounds.height - 4.0f) {
        y = anchor_.y - bubble_h - kGap; // flip above the anchor
    }
    x = std::clamp(x, bounds.x + 4.0f, bounds.x + bounds.width - bubble_w - 4.0f);
    y = std::clamp(y, bounds.y + 4.0f, bounds.y + bounds.height - bubble_h - 4.0f);
    const Layout::Rect bubble{x, y, bubble_w, bubble_h};

    const auto FA = [p](Theme::Color col) {
        col.a *= p;
        return col;
    };
    // Soft shadow, then the inverted pill.
    renderer.FillRect({x - 1.0f, y + 2.0f, bubble_w + 2.0f, bubble_h},
                      {0.0f, 0.0f, 0.0f, 0.18f * p}, tokens.radius_medium + 2.0f);
    renderer.FillRect(bubble, FA(tokens.foreground), tokens.radius_medium);
    const float tx = x + kPadX;
    const float ty = y + (bubble_h - text_size.height) * 0.5f;
    renderer.DrawText(text_, Layout::Rect{tx, ty,
                                          std::min(text_size.width, max_text_w),
                                          text_size.height},
                      style);
}

Layout::Size Tooltip::GetMinSize() const {
    return {0.0f, 0.0f};
}

Layout::Size Tooltip::GetPreferredSize() const {
    return {kMaxWidth, 24.0f};
}

Layout::Size Tooltip::GetMaxSize() const {
    return {kMaxWidth, 200.0f};
}

} // namespace XER::UI::Widgets
