#ifndef XER_UI_WIDGETS_DIALOG_H
#define XER_UI_WIDGETS_DIALOG_H

#include <functional>
#include <memory>
#include <string>

#include "widgets/widget.h"

namespace XER::UI::Widgets {

// Headless UI-style modal dialog shown in the overlay layer: a dimmed,
// translucent scrim with a centered glass panel containing a title,
// optional content widget and confirm/cancel buttons. Create via
// Dialog::Create() and show with Widget::ShowOverlay(). Closes on Escape,
// scrim click or the close button. WasConfirmed() reports whether the
// confirm action (button or Enter) was used — dismissal paths leave it
// false so callers can distinguish submit from cancel.
class Dialog final : public Widget {
public:
    static std::shared_ptr<Dialog> Create(
        std::string title,
        std::shared_ptr<Widget> content = nullptr,
        std::string confirm_label = "OK",
        std::string cancel_label = ""
    );

    void SetTitle(std::string title);
    void SetContent(std::shared_ptr<Widget> content);
    void SetOnClosed(std::function<void()> callback);
    void Close();
    // True only when the dialog was closed via the confirm button or Enter.
    bool WasConfirmed() const { return confirmed_; }

    void Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) override;
    void Layout(const Layout::Rect& bounds) override;
    using Widget::Layout;
    void Layout(const Layout::Rect& bounds, const Theme::Tokens& tokens) override;
    bool HandleEvent(const Event& event) override;

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

private:
    explicit Dialog(std::string title, std::shared_ptr<Widget> content,
                    std::string confirm_label, std::string cancel_label);

    Layout::Rect PanelRect(const Layout::Rect& bounds) const;

    std::string title_;
    std::string confirm_label_;
    std::string cancel_label_;
    std::shared_ptr<Widget> content_;
    Layout::Rect close_rect_;
    Layout::Rect confirm_rect_;
    Layout::Rect cancel_rect_;
    bool closed_ = false;
    bool confirmed_ = false;
    std::function<void()> on_closed_;
};

} // namespace XER::UI::Widgets

#endif // XER_UI_WIDGETS_DIALOG_H
