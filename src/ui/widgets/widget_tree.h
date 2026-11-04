#ifndef XER_UI_WIDGETS_WIDGET_TREE_H
#define XER_UI_WIDGETS_WIDGET_TREE_H

#include <memory>
#include <vector>

#include "layout/geometry.h"
#include "widgets/widget.h"

namespace XER::UI::Theme {
struct Tokens;
}

namespace XER::UI::Rendering {
class Renderer;
}

namespace XER::UI::Widgets {

class WidgetTree {
public:
    void SetRoot(std::shared_ptr<Widget> root);
    std::shared_ptr<Widget> GetRoot() const;
    void Layout(const Layout::Rect& bounds);
    void Layout(const Layout::Rect& bounds, const Theme::Tokens& tokens);
    void Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) const;
    bool HandleEvent(const Event& event);

private:
    void ClearFocus(const std::shared_ptr<Widget>& widget);
    void RenderSubtree(
        const std::shared_ptr<Widget>& widget,
        Rendering::Renderer& renderer,
        const Theme::Tokens& tokens
    ) const;
    void CollectFocusable(
        const std::shared_ptr<Widget>& widget,
        bool ancestors_available,
        std::vector<std::shared_ptr<Widget>>& widgets
    ) const;
    std::shared_ptr<Widget> FindHitTarget(
        const std::shared_ptr<Widget>& widget,
        const Layout::Point& position,
        bool ancestors_available
    ) const;
    bool MoveFocus(bool backwards);
    void SetFocusedWidget(const std::shared_ptr<Widget>& widget);

    std::shared_ptr<Widget> root_;
    std::weak_ptr<Widget> left_mouse_down_target_;
    std::weak_ptr<Widget> synthesized_click_target_;
    Layout::Point synthesized_click_position_;
    bool suppress_duplicate_click_ = false;
};

} // namespace XER::UI::Widgets

#endif // XER_UI_WIDGETS_WIDGET_TREE_H
