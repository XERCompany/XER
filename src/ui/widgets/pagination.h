#ifndef XER_UI_WIDGETS_PAGINATION_H
#define XER_UI_WIDGETS_PAGINATION_H

#include <functional>
#include <vector>

#include "widgets/widget.h"

namespace XER::UI::Widgets {

// shadcn-style pagination: previous/next chevrons plus a windowed row of
// page buttons with ellipsis for large page counts.
class Pagination final : public Widget {
public:
    Pagination(int page_count = 1, int current_page = 0);

    void SetPageCount(int count);
    void SetCurrentPage(int page);
    int GetCurrentPage() const { return current_page_; }
    void SetOnPageChanged(std::function<void(int)> callback);

    void Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) override;
    void Layout(const Layout::Rect& bounds) override;
    using Widget::Layout;
    bool HandleEvent(const Event& event) override;

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

private:
    // Builds the visible page sequence: page indices or -1 for ellipsis.
    std::vector<int> VisiblePages() const;
    int HitButton(const Layout::Point& position) const; // index into buttons_
    void GoTo(int page);

    int page_count_ = 1;
    int current_page_ = 0;
    int hovered_button_ = -1;
    std::vector<Layout::Rect> button_rects_;
    // Parallel to button_rects_: page index, or -2 = prev, -3 = next, -1 = ellipsis.
    std::vector<int> button_pages_;
    std::function<void(int)> on_page_changed_;
};

} // namespace XER::UI::Widgets

#endif // XER_UI_WIDGETS_PAGINATION_H
