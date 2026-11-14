#ifndef XER_UI_WIDGETS_MARKER_H
#define XER_UI_WIDGETS_MARKER_H

#include "widgets/widget.h"

#include <string>

namespace XER::UI::Widgets {

// Marker — highlighted text run: rounded tinted background behind the label.
class Marker final : public Widget {
public:
    explicit Marker(std::string text = {});

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

#endif // XER_UI_WIDGETS_MARKER_H
