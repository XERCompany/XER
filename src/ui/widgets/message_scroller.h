#ifndef XER_UI_WIDGETS_MESSAGE_SCROLLER_H
#define XER_UI_WIDGETS_MESSAGE_SCROLLER_H

#include <memory>
#include <vector>

#include "widgets/widget.h"

namespace XER::UI::Widgets {

// MessageScroller — scrollable vertical list for Message widgets.
// Newest messages anchor to the bottom (chat behavior); the view
// auto-scrolls to the bottom when content grows unless the user has
// scrolled up. Mouse wheel scrolls; children are clipped to bounds.
class MessageScroller final : public Widget {
public:
    MessageScroller();

    void AddMessage(std::shared_ptr<Widget> message);
    void Clear();
    void ScrollToBottom();
    float GetScrollY() const { return scroll_y_; }

    void Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) override;
    void Layout(const Layout::Rect& bounds) override;
    using Widget::Layout;
    bool HandleEvent(const Event& event) override;

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;
    float GetContentHeight() const override;

private:
    void RelayoutChildren(const Theme::Tokens& tokens);
    float ContentHeight() const;

    std::vector<std::shared_ptr<Widget>> messages_;
    float scroll_y_ = 0.0f;
    bool stick_bottom_ = true;
};

} // namespace XER::UI::Widgets

#endif // XER_UI_WIDGETS_MESSAGE_SCROLLER_H
