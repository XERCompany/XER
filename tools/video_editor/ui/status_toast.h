// Status toast: small transient notification cards stacked in the
// widget's bottom-right corner — the Premiere/AE "info bubble" that
// confirms an action or flags an error, then fades away on its own.
//
//   ┌───────────────────────────────┐
//   │▌ export queued: promo.mp4     │   accent bar = info
//   └───────────────────────────────┘
//   ┌───────────────────────────────┐
//   │▌ export failed: no encoder    │   danger bar = error
//   └───────────────────────────────┘
//
// Push() stacks up to 3 messages (a 4th push drops the oldest). Tick
// ages them out: ~4 s lifetime with a short fade-in and a longer
// fade-out. The widget draws everything itself and never consumes
// input — host it as a full-window overlay and clicks fall through.
#pragma once

#include <deque>
#include <string>

#include "widgets/widget.h"

namespace XER::VideoEditor::Ui {

class StatusToastWidget final : public XER::UI::Widgets::Widget {
public:
    StatusToastWidget();

    // `is_error` selects the danger accent; otherwise the info accent.
    // Keeps at most kMaxToasts — overflow drops the oldest card.
    void Push(const std::string& text, bool is_error);
    int Count() const { return static_cast<int>(toasts_.size()); }

    void Render(XER::UI::Rendering::Renderer& renderer,
                const XER::UI::Theme::Tokens& tokens) override;
    void Layout(const XER::UI::Layout::Rect& bounds) override;
    using Widget::Layout;
    // Clicks fall through to whatever is below the overlay.
    bool HandleEvent(const XER::UI::Widgets::Event& event) override;
    // Ages + expires cards; call from the host's frame tick.
    void Tick(double dt_seconds) override;

    XER::UI::Layout::Size GetMinSize() const override {
        return {1.0f, 1.0f};
    }
    XER::UI::Layout::Size GetPreferredSize() const override {
        return {1.0f, 1.0f};
    }
    XER::UI::Layout::Size GetMaxSize() const override {
        return {100000.0f, 100000.0f};
    }

private:
    struct Toast {
        std::string text;
        bool is_error = false;
        double age = 0.0;   // seconds since Push
    };

    static constexpr std::size_t kMaxToasts = 3;
    static constexpr double kLifetimeSeconds = 4.0;
    static constexpr double kFadeInSeconds = 0.15;
    static constexpr double kFadeOutSeconds = 0.6;

    std::deque<Toast> toasts_;   // front = oldest (top), back = newest
};

}  // namespace XER::VideoEditor::Ui
