#ifndef XER_UI_WIDGETS_DIRECTION_H
#define XER_UI_WIDGETS_DIRECTION_H

#include "widgets/widget.h"

namespace XER::UI::Widgets {

// Direction — provides a text-direction context (ltr/rtl) for its subtree.
// Children resolve it through Widget::EffectiveTextDirection(); the widget
// itself is a plain container that gives children the full content rect.
class Direction final : public Widget {
public:
    explicit Direction(TextDirection direction = TextDirection::Ltr);

    void SetDirection(TextDirection direction);
    TextDirection GetDirection() const { return direction_; }

    void Render(Rendering::Renderer&, const Theme::Tokens&) override {}
    void Layout(const Layout::Rect& bounds) override;
    using Widget::Layout;
    bool HandleEvent(const Event&) override { return false; }

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

private:
    TextDirection direction_ = TextDirection::Ltr;
};

} // namespace XER::UI::Widgets

#endif // XER_UI_WIDGETS_DIRECTION_H
