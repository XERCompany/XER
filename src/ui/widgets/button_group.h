#ifndef XER_UI_WIDGETS_BUTTON_GROUP_H
#define XER_UI_WIDGETS_BUTTON_GROUP_H

#include "widgets/widget.h"

namespace XER::UI::Widgets {

// ButtonGroup — joined button strip: children edge-to-edge inside one
// bordered surface (horizontal or vertical), dividers between items.
class ButtonGroup final : public Widget {
public:
    enum class Orientation { Horizontal, Vertical };

    explicit ButtonGroup(Orientation orientation = Orientation::Horizontal);

    void SetOrientation(Orientation orientation);
    Orientation GetOrientation() const { return orientation_; }

    void Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) override;
    void Layout(const Layout::Rect& bounds) override;
    using Widget::Layout;
    bool HandleEvent(const Event& event) override;

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

private:
    std::vector<Layout::Rect> ChildRects(const Layout::Rect& bounds) const;

    Orientation orientation_;
};

} // namespace XER::UI::Widgets

#endif // XER_UI_WIDGETS_BUTTON_GROUP_H
