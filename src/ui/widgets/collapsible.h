#ifndef XER_UI_WIDGETS_COLLAPSIBLE_H
#define XER_UI_WIDGETS_COLLAPSIBLE_H

#include "widgets/widget.h"

#include <functional>

namespace XER::UI::Widgets {

// Collapsible — show/hide container with animated height. Children are
// clipped to the animated viewport.
class Collapsible final : public Widget {
public:
    explicit Collapsible(bool open = false);

    void SetOpen(bool open);
    bool IsOpen() const { return open_; }
    // 0.0 = fully closed, 1.0 = fully open (animated value).
    float GetProgress() const { return progress_; }
    void SetOnToggled(std::function<void(bool)> callback);

    void Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) override;
    void Layout(const Layout::Rect& bounds) override;
    using Widget::Layout;
    bool HandleEvent(const Event& event) override;

    void Tick(double dt_seconds) override;

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

private:
    bool open_;
    float progress_;
    float content_height_ = 0.0f;
    std::function<void(bool)> on_toggled_;
};

} // namespace XER::UI::Widgets

#endif // XER_UI_WIDGETS_COLLAPSIBLE_H
