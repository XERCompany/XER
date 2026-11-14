#ifndef XER_UI_WIDGETS_ITEM_H
#define XER_UI_WIDGETS_ITEM_H

#include "widgets/widget.h"

#include <string>

namespace XER::UI::Widgets {

// Item — list row: optional leading child, title + description column,
// optional trailing child.
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
};

} // namespace XER::UI::Widgets

#endif // XER_UI_WIDGETS_ITEM_H
