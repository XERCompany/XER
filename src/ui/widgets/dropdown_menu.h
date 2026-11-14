#ifndef XER_UI_WIDGETS_DROPDOWN_MENU_H
#define XER_UI_WIDGETS_DROPDOWN_MENU_H

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "widgets/context_menu.h"
#include "widgets/widget.h"

namespace XER::UI::Widgets {

// DropdownMenu — a trigger widget that opens a ContextMenu below it on
// click. Items mirror ContextMenu::Item (label/action/enabled/checked/
// shortcut/danger/separator_before) so JS callers pass the same shape.
class DropdownMenu final : public Widget {
public:
    using Item = ContextMenu::Item;

    DropdownMenu();
    ~DropdownMenu() override;

    void SetTrigger(std::shared_ptr<Widget> trigger);
    void SetItems(std::vector<Item> items);
    const std::vector<Item>& GetItems() const { return items_; }
    void Open();
    void Close();
    bool IsOpen() const;
    void SetOnOpenChange(std::function<void(bool)> callback);

    void Render(Rendering::Renderer&, const Theme::Tokens&) override {}
    void Layout(const Layout::Rect& bounds) override;
    using Widget::Layout;
    bool HandleEvent(const Event&) override { return false; }

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

private:
    void SyncOpen(bool open);

    std::shared_ptr<Widget> trigger_;
    std::vector<Item> items_;
    bool open_ = false;
    std::weak_ptr<Widget> menu_;
    std::function<void(bool)> on_open_change_;
};

} // namespace XER::UI::Widgets

#endif // XER_UI_WIDGETS_DROPDOWN_MENU_H
