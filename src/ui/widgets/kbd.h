#ifndef XER_UI_WIDGETS_KBD_H
#define XER_UI_WIDGETS_KBD_H

#include "widgets/widget.h"

#include <string>

namespace XER::UI::Widgets {

// Kbd — keyboard key cap: bordered surface box with centered label text.
class Kbd final : public Widget {
public:
    explicit Kbd(std::string text = {});

    void SetText(std::string text);
    const std::string& GetText() const { return text_; }

    void Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) override;
    void Layout(const Layout::Rect& bounds) override;
    using Widget::Layout;
    bool HandleEvent(const Event& event) override;

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

private:
    std::string text_;
};

} // namespace XER::UI::Widgets

#endif // XER_UI_WIDGETS_KBD_H
