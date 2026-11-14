#ifndef XER_UI_WIDGETS_DRAWER_H
#define XER_UI_WIDGETS_DRAWER_H

#include <functional>
#include <memory>
#include <string>

#include "motion/motion.h"
#include "widgets/widget.h"

namespace XER::UI::Widgets {

// Drawer — bottom-anchored modal sheet (vaul-style): scrim + panel that
// slides up from the bottom edge. Same lifecycle as Sheet/Dialog:
// ShowOverlay to present, Close()/scrim/Escape to dismiss.
class Drawer final : public Widget {
public:
    static std::shared_ptr<Drawer> Create(std::string title = {},
                                          std::shared_ptr<Widget> content = {});

    void SetTitle(std::string title);
    void SetContent(std::shared_ptr<Widget> content);
    void SetSnapPoint(float fraction);  // 0..1 of client height
    void SetOnClosed(std::function<void()> callback);
    void Close();
    bool WasDismissed() const { return dismissed_; }

    void Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) override;
    void Layout(const Layout::Rect& bounds) override;
    using Widget::Layout;
    void Tick(double dt_seconds) override;
    bool HandleEvent(const Event& event) override;

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

private:
    explicit Drawer(std::string title, std::shared_ptr<Widget> content);
    Layout::Rect PanelRect(const Layout::Rect& bounds) const;

    std::string title_;
    std::shared_ptr<Widget> content_;
    float snap_fraction_ = 0.5f;
    Layout::Rect close_rect_{};
    Layout::Rect handle_rect_{};
    bool dragging_ = false;
    float drag_offset_ = 0.0f;
    float last_drag_y_ = 0.0f;
    bool closed_ = false;
    bool dismissed_ = false;
    bool ticked_ = false;
    bool rendered_ = false;
    Motion::Tween open_t_;
    std::function<void()> on_closed_;
};

} // namespace XER::UI::Widgets

#endif // XER_UI_WIDGETS_DRAWER_H
