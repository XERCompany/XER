#ifndef XER_UI_WIDGETS_SWITCH_H
#define XER_UI_WIDGETS_SWITCH_H

#include <functional>

#include "motion/motion.h"
#include "widgets/widget.h"

namespace XER::UI::Widgets {

class Switch final : public Widget {
public:
    explicit Switch(bool checked = false);

    bool IsChecked() const;
    void SetChecked(bool checked);
    void SetOnChanged(std::function<void(bool)> callback);

    void Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) override;
    void Layout(const Layout::Rect& bounds) override;
    using Widget::Layout;
    void Tick(double dt_seconds) override;
    bool HandleEvent(const Event& event) override;

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

private:
    void Toggle();

    bool checked_ = false;
    std::function<void(bool)> on_changed_;

    // Motion: thumb travel 0..1 (springs between states, keeps velocity on
    // interrupt) and a press-stretch tween while the pointer is held.
    Motion::Spring thumb_t_;
    Motion::Tween press_t_;
    bool held_ = false;
};

} // namespace XER::UI::Widgets

#endif // XER_UI_WIDGETS_SWITCH_H
