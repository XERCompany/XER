#ifndef XER_UI_WIDGETS_BREADCRUMB_H
#define XER_UI_WIDGETS_BREADCRUMB_H

#include <functional>
#include <string>
#include <vector>

#include "widgets/widget.h"

namespace XER::UI::Widgets {

// shadcn-style breadcrumb: a horizontal trail of items separated by chevron
// glyphs. The last item renders emphasized; earlier items are muted and
// clickable when a navigate callback is set.
class Breadcrumb final : public Widget {
public:
    void SetItems(std::vector<std::string> items);
    void SetOnNavigate(std::function<void(int)> callback);

    void Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) override;
    void Layout(const Layout::Rect& bounds) override;
    bool HandleEvent(const Event& event) override;

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

private:
    int HitItem(const Layout::Point& position) const;

    std::vector<std::string> items_;
    std::vector<Layout::Rect> item_rects_;
    int hovered_item_ = -1;
    std::function<void(int)> on_navigate_;
};

} // namespace XER::UI::Widgets

#endif // XER_UI_WIDGETS_BREADCRUMB_H
