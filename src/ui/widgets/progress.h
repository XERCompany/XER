#ifndef XER_UI_WIDGETS_PROGRESS_H
#define XER_UI_WIDGETS_PROGRESS_H

#include "motion/motion.h"
#include "widgets/widget.h"

namespace XER::UI::Widgets {

class Progress final : public Widget {
public:
    explicit Progress(float value = 0.0f);

    void SetValue(float value);
    float GetValue() const;

    void Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) override;
    void Layout(const Layout::Rect& bounds) override;
    using Widget::Layout;
    void Tick(double dt_seconds) override;
    bool HandleEvent(const Event& event) override;

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

private:
    float value_;
    // Displayed value chases value_ on an ease-out tween.
    Motion::Tween shown_;
};

} // namespace XER::UI::Widgets

#endif // XER_UI_WIDGETS_PROGRESS_H
