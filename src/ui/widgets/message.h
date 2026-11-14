#ifndef XER_UI_WIDGETS_MESSAGE_H
#define XER_UI_WIDGETS_MESSAGE_H

#include <string>

#include "widgets/bubble.h"
#include "widgets/widget.h"

namespace XER::UI::Widgets {

// Message — chat message row: optional avatar circle (sender initial),
// sender name + timestamp meta line, and a Bubble for the body.
// Own messages align right with accent bubbles; others align left.
class Message final : public Widget {
public:
    Message(std::string sender = {}, std::string text = {},
            std::string time = {}, Bubble::Align align = Bubble::Align::Other);

    void SetSender(std::string sender);
    const std::string& GetSender() const { return sender_; }
    void SetText(std::string text);
    const std::string& GetText() const { return text_; }
    void SetTime(std::string time);
    const std::string& GetTime() const { return time_; }
    void SetAlign(Bubble::Align align);
    Bubble::Align GetAlign() const { return align_; }

    void Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) override;
    void Layout(const Layout::Rect& bounds) override;
    using Widget::Layout;
    bool HandleEvent(const Event&) override { return false; }

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

private:
    std::string sender_;
    std::string text_;
    std::string time_;
    Bubble::Align align_ = Bubble::Align::Other;
};

} // namespace XER::UI::Widgets

#endif // XER_UI_WIDGETS_MESSAGE_H
