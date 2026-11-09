#ifndef XER_UI_WIDGETS_SELECT_H
#define XER_UI_WIDGETS_SELECT_H

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "widgets/widget.h"

namespace XER::UI::Widgets {

// shadcn-style select: a button-like field that opens a dropdown list in the
// overlay layer. Without a WidgetTree (detached use) it cycles options on
// click instead of opening a popup.
class Select final : public Widget {
public:
    Select(std::vector<std::string> options = {}, int selected = -1);
    ~Select() override;

    void SetOptions(std::vector<std::string> options);
    void SetSelectedIndex(int index);
    int GetSelectedIndex() const { return selected_index_; }
    const std::string& GetSelectedText() const;
    void SetPlaceholder(std::string placeholder);
    void SetOnChanged(std::function<void(int)> callback);

    void Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) override;
    void Layout(const Layout::Rect& bounds) override;
    using Widget::Layout;
    bool HandleEvent(const Event& event) override;

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

private:
    void OpenDropdown();
    void CloseDropdown();
    bool IsDropdownOpen() const;

    std::vector<std::string> options_;
    int selected_index_ = -1;
    std::string placeholder_ = "Select...";
    bool open_ = false;
    // The open dropdown overlay (SelectOverlay, defined in select.cc).
    // Tracked weakly so SetOptions/Layout can refresh it while open and the
    // destructor can close it before the tree loses the owner.
    std::weak_ptr<Widget> dropdown_;
    std::function<void(int)> on_changed_;
};

} // namespace XER::UI::Widgets

#endif // XER_UI_WIDGETS_SELECT_H
