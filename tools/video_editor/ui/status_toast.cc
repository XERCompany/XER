#include "status_toast.h"

#include <algorithm>

#include "rendering/renderer.h"
#include "theme/tokens.h"

namespace XER::VideoEditor::Ui {
namespace {

using XER::UI::Layout::Rect;
using XER::UI::Theme::Color;

constexpr float kCardWidth = 300.0f;
constexpr float kCardHeight = 40.0f;
constexpr float kCardGap = 8.0f;
constexpr float kEdgePad = 14.0f;
constexpr float kBarWidth = 4.0f;

Color WithAlpha(Color color, float alpha) {
    color.a *= alpha;
    return color;
}

}  // namespace

StatusToastWidget::StatusToastWidget() {
    SetFocusable(false);   // overlay chrome — never steals focus
    SetClipsChildren(true);
}

void StatusToastWidget::Push(const std::string& text, bool is_error) {
    if (text.empty()) return;
    toasts_.push_back(Toast{text, is_error, 0.0});
    while (toasts_.size() > kMaxToasts) {
        toasts_.pop_front();   // newest wins; drop the oldest
    }
    Invalidate();
}

void StatusToastWidget::Render(
    XER::UI::Rendering::Renderer& renderer,
    const XER::UI::Theme::Tokens& tokens) {
    if (toasts_.empty()) return;
    const Rect bounds = GetBounds();
    const float card_w = std::min(kCardWidth, bounds.width - kEdgePad * 2.0f);
    if (card_w < 40.0f) return;

    // Stack upward from the bottom-right corner; newest (deque back)
    // sits closest to the bottom edge.
    float y = bounds.y + bounds.height - kEdgePad - kCardHeight;
    for (auto it = toasts_.rbegin(); it != toasts_.rend(); ++it) {
        const Toast& toast = *it;
        // Alpha envelope: quick fade-in, hold, longer fade-out.
        float alpha = 1.0f;
        if (toast.age < kFadeInSeconds) {
            alpha = static_cast<float>(toast.age / kFadeInSeconds);
        } else {
            const double left = kLifetimeSeconds - toast.age;
            if (left < kFadeOutSeconds) {
                alpha = static_cast<float>(
                    std::max(0.0, left) / kFadeOutSeconds);
            }
        }
        const Rect card{bounds.x + bounds.width - kEdgePad - card_w, y,
                        card_w, kCardHeight};
        renderer.FillRect(card, WithAlpha(tokens.panel, 0.96f * alpha),
                          tokens.radius_medium);
        renderer.StrokeRect(card, WithAlpha(tokens.border, alpha), 1.0f,
                            tokens.radius_medium);
        // Accent (info) / danger (error) left bar.
        const Color bar = toast.is_error ? tokens.danger : tokens.accent;
        renderer.FillRect({card.x, card.y + 6.0f, kBarWidth,
                           card.height - 12.0f},
                          WithAlpha(bar, alpha), kBarWidth * 0.5f);
        renderer.DrawText(
            toast.text,
            {card.x + kBarWidth + 10.0f, card.y, card.width - kBarWidth - 20.0f,
             card.height},
            XER::UI::Rendering::TextStyle{
                WithAlpha(tokens.foreground, alpha), tokens.font_small,
                500});
        y -= kCardHeight + kCardGap;
        if (y < bounds.y) break;
    }
}

void StatusToastWidget::Layout(const XER::UI::Layout::Rect& bounds) {
    SetBounds(bounds);
}

bool StatusToastWidget::HandleEvent(
    const XER::UI::Widgets::Event& event) {
    (void)event;
    return false;   // click-through overlay
}

void StatusToastWidget::Tick(double dt_seconds) {
    if (toasts_.empty()) return;
    for (Toast& toast : toasts_) {
        toast.age += std::max(0.0, dt_seconds);
    }
    while (!toasts_.empty() && toasts_.front().age >= kLifetimeSeconds) {
        toasts_.pop_front();
    }
    Invalidate();   // keep repainting while any card fades
}

}  // namespace XER::VideoEditor::Ui
