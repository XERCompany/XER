#ifndef XER_UI_WIDGETS_SLIDER_H
#define XER_UI_WIDGETS_SLIDER_H

#include <functional>

#include "widgets/widget.h"

namespace XER::UI::Widgets {

class Slider final : public Widget {
public:
    explicit Slider(float minimum = 0.0f, float maximum = 1.0f, float value = 0.0f);

    void SetRange(float minimum, float maximum);
    void SetValue(float value);
    float GetValue() const;
    void SetOnValueChanged(std::function<void(float)> callback);

    void Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) override;
    void Layout(const Layout::Rect& bounds) override;
    bool HandleEvent(const Event& event) override;

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

private:
    void SetValueFromPosition(float x);

    float minimum_;
    float maximum_;
    float value_;
    std::function<void(float)> on_value_changed_;
};

} // namespace XER::UI::Widgets

#endif // XER_UI_WIDGETS_SLIDER_H
