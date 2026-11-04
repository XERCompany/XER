#ifndef XER_UI_WIDGETS_SEPARATOR_H
#define XER_UI_WIDGETS_SEPARATOR_H

#include "widgets/widget.h"

namespace XER::UI::Widgets {

enum class Orientation {
    Horizontal,
    Vertical
};

class Separator final : public Widget {
public:
    explicit Separator(Orientation orientation = Orientation::Horizontal);

    void Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) override;
    void Layout(const Layout::Rect& bounds) override;
    bool HandleEvent(const Event& event) override;

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

private:
    Orientation orientation_;
};

} // namespace XER::UI::Widgets

#endif // XER_UI_WIDGETS_SEPARATOR_H
