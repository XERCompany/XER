#ifndef XER_UI_WIDGETS_ITEM_H
#define XER_UI_WIDGETS_ITEM_H

#include "widgets/widget.h"

#include <chrono>
#include <functional>
#include <string>

namespace XER::UI::Widgets {

// Item — list row: optional leading child, title + description column,
// optional trailing child. A single child occupies the LEADING slot;
// with two or more children the first is leading and the last trailing.
//
// Rows are interactive when SetOnClick/SetOnDoubleClick is wired: the
// Item consumes Click, invokes the hooks, and the tree's click bubbling
// then moves keyboard focus to the nearest focusable ancestor (the
// list panel) so arrow-key navigation keeps working. Rows without
// hooks stay event-transparent — their clicks bubble to the parent.
class Item final : public Widget {
public:
    Item();

    void SetTitle(std::string text);
    const std::string& GetTitle() const { return title_; }
    void SetDescription(std::string text);
    const std::string& GetDescription() const { return description_; }
    // Marks the row selected (tinted background).
    void SetSelected(bool selected);
    bool IsSelected() const { return selected_; }

    // Fired on every left Click (list-panel convention: select the row).
    void SetOnClick(std::function<void()> callback);
    // Fired additionally on a rapid second Click on this same row
    // (list-panel convention: apply/open the row). The double-click
    // window matches the panels' 450ms; both hooks fire on the second
    // click, so a handler only needs to do the "apply" part.
    void SetOnDoubleClick(std::function<void()> callback);

    void Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) override;
    void Layout(const Layout::Rect& bounds) override;
    using Widget::Layout;
    bool HandleEvent(const Event& event) override;

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

private:
    std::string title_;
    std::string description_;
    bool selected_ = false;
    std::function<void()> on_click_;
    std::function<void()> on_double_click_;
    std::chrono::steady_clock::time_point last_click_time_{};
};

} // namespace XER::UI::Widgets

#endif // XER_UI_WIDGETS_ITEM_H
