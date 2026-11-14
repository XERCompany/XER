#ifndef XER_UI_WIDGETS_INPUT_GROUP_H
#define XER_UI_WIDGETS_INPUT_GROUP_H

#include "widgets/widget.h"

#include <string>

namespace XER::UI::Widgets {

// InputGroup — bordered field shell: optional prefix/suffix addon labels
// flank the child control inside one rounded border.
class InputGroup final : public Widget {
public:
    InputGroup();

    void SetPrefix(std::string text);
    const std::string& GetPrefix() const { return prefix_; }
    void SetSuffix(std::string text);
    const std::string& GetSuffix() const { return suffix_; }

    void Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) override;
    void Layout(const Layout::Rect& bounds) override;
    using Widget::Layout;
    bool HandleEvent(const Event& event) override;

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

private:
    std::string prefix_;
    std::string suffix_;
};

} // namespace XER::UI::Widgets

#endif // XER_UI_WIDGETS_INPUT_GROUP_H
