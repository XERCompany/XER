#ifndef XER_UI_WIDGETS_ASPECT_RATIO_H
#define XER_UI_WIDGETS_ASPECT_RATIO_H

#include "widgets/widget.h"

namespace XER::UI::Widgets {

// AspectRatio — container fitting children into the largest
// width/height-proportioned rectangle centered inside its bounds.
class AspectRatio final : public Widget {
public:
    explicit AspectRatio(float ratio = 16.0f / 9.0f);

    void SetRatio(float ratio);
    float GetRatio() const { return ratio_; }

    void Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) override;
    void Layout(const Layout::Rect& bounds) override;
    using Widget::Layout;
    bool HandleEvent(const Event& event) override;

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

private:
    Layout::Rect ContentRect(const Layout::Rect& bounds) const;

    float ratio_;
};

} // namespace XER::UI::Widgets

#endif // XER_UI_WIDGETS_ASPECT_RATIO_H
