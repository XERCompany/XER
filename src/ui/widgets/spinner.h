#ifndef XER_UI_WIDGETS_SPINNER_H
#define XER_UI_WIDGETS_SPINNER_H

#include "widgets/widget.h"

namespace XER::UI::Widgets {

class Spinner final : public Widget {
public:
    explicit Spinner(float phase = 0.0f);

    void SetPhase(float phase);

    void Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) override;
    void Layout(const Layout::Rect& bounds) override;
    using Widget::Layout;
    void Tick(double dt_seconds) override;
    bool HandleEvent(const Event& event) override;

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

private:
    float phase_;
};

} // namespace XER::UI::Widgets

#endif // XER_UI_WIDGETS_SPINNER_H
