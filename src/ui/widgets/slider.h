#ifndef XER_UI_WIDGETS_SLIDER_H
#define XER_UI_WIDGETS_SLIDER_H

#include <functional>

#include "motion/motion.h"
#include "widgets/widget.h"

namespace XER::UI::Widgets {

class Slider final : public Widget {
public:
    explicit Slider(float minimum = 0.0f, float maximum = 1.0f, float value = 0.0f);

    void SetRange(float minimum, float maximum);
    void SetValue(float value);
    float GetValue() const;
    // True between left MouseDown on the track/thumb and the matching MouseUp.
    bool IsDragging() const;
    void SetOnValueChanged(std::function<void(float)> callback);

    void Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) override;
    void Layout(const Layout::Rect& bounds) override;
    using Widget::Layout;
    void Tick(double dt_seconds) override;
    bool HandleEvent(const Event& event) override;

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

private:
    void SetValueFromPosition(float x);

    float minimum_;
    float maximum_;
    float value_;
    bool dragging_ = false;
    std::function<void(float)> on_value_changed_;
    // Thumb swell: eases toward 1 on hover/grab, back to 0 at rest.
    Motion::Tween swell_;
};

} // namespace XER::UI::Widgets

#endif // XER_UI_WIDGETS_SLIDER_H
