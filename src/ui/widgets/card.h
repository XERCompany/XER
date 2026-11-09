#ifndef XER_UI_WIDGETS_CARD_H
#define XER_UI_WIDGETS_CARD_H

#include "widgets/widget.h"

namespace XER::UI::Widgets {

class Card final : public Widget {
public:
    void Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) override;
    void Layout(const Layout::Rect& bounds) override;
    using Widget::Layout;
    void Layout(const Layout::Rect& bounds, const Theme::Tokens& tokens) override;
    bool HandleEvent(const Event& event) override;

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;
};

} // namespace XER::UI::Widgets

#endif // XER_UI_WIDGETS_CARD_H
