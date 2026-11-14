#ifndef XER_UI_WIDGETS_BUBBLE_H
#define XER_UI_WIDGETS_BUBBLE_H

#include <string>

#include "widgets/widget.h"

namespace XER::UI::Widgets {

// Bubble — chat message bubble. `align` selects the side (Own = accent
// surface right-aligned, Other = muted surface left-aligned) with a small
// tail corner on the aligned edge.
class Bubble final : public Widget {
public:
    enum class Align { Other, Own };

    Bubble(std::string text = {}, Align align = Align::Other);

    void SetText(std::string text);
    const std::string& GetText() const { return text_; }
    void SetAlign(Align align);
    Align GetAlign() const { return align_; }

    void Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) override;
    void Layout(const Layout::Rect& bounds) override;
    using Widget::Layout;
    bool HandleEvent(const Event&) override { return false; }

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

private:
    std::string text_;
    Align align_ = Align::Other;
};

} // namespace XER::UI::Widgets

#endif // XER_UI_WIDGETS_BUBBLE_H
