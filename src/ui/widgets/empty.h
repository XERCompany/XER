#ifndef XER_UI_WIDGETS_EMPTY_H
#define XER_UI_WIDGETS_EMPTY_H

#include "widgets/widget.h"

#include <string>

namespace XER::UI::Widgets {

// Empty — empty-state block: icon slot (first child), centered title +
// description, action children below.
class Empty final : public Widget {
public:
    Empty();

    void SetTitle(std::string text);
    const std::string& GetTitle() const { return title_; }
    void SetDescription(std::string text);
    const std::string& GetDescription() const { return description_; }

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
};

} // namespace XER::UI::Widgets

#endif // XER_UI_WIDGETS_EMPTY_H
