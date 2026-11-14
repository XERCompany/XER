#ifndef XER_UI_WIDGETS_FIELD_H
#define XER_UI_WIDGETS_FIELD_H

#include "widgets/widget.h"

#include <string>

namespace XER::UI::Widgets {

// Field — form field shell: label on top, child control in the middle,
// description or error line below.
class Field final : public Widget {
public:
    Field();

    void SetLabel(std::string text);
    const std::string& GetLabel() const { return label_; }
    void SetDescription(std::string text);
    const std::string& GetDescription() const { return description_; }
    // Non-empty error replaces the description and renders in danger color.
    void SetError(std::string text);
    const std::string& GetError() const { return error_; }

    void Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) override;
    void Layout(const Layout::Rect& bounds) override;
    using Widget::Layout;
    bool HandleEvent(const Event& event) override;

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

private:
    std::string label_;
    std::string description_;
    std::string error_;
};

} // namespace XER::UI::Widgets

#endif // XER_UI_WIDGETS_FIELD_H
