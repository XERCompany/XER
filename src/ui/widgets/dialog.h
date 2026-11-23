#ifndef XER_UI_WIDGETS_DIALOG_H
#define XER_UI_WIDGETS_DIALOG_H

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "widgets/button.h"
#include "widgets/widget.h"

namespace XER::UI::Widgets {

// Headless UI-style modal dialog shown in the overlay layer: a dimmed,
// translucent scrim with a centered glass panel containing a title,
// optional content widget and confirm/cancel buttons. Create via
// Dialog::Create() and show with Widget::ShowOverlay(). Closes on Escape,
// scrim click or the close button. WasConfirmed() reports whether the
// confirm action (button or Enter) was used — dismissal paths leave it
// false so callers can distinguish submit from cancel.
//
// Beyond the stock confirm/cancel pair it models the views::DialogDelegate
// / QDialog surface: a custom button row with accept/reject semantics,
// modal vs modeless scrim, header dragging, a resize grip, and an
// initial-focus slot.
class Dialog final : public Widget {
public:
    // QDialog::DialogCode — how the dialog was dismissed.
    enum class Result {
        None,
        Accepted,
        Rejected
    };

    // views::DialogDelegate-style button row entry. `result` applies
    // after `action` runs: Accepted/Rejected close the dialog with that
    // code, while Result::None leaves it open — the "Apply" pattern.
    struct Button {
        std::string label;
        ButtonStyle style = ButtonStyle::Secondary;
        Result result = Result::None;
        std::function<void()> action;
    };

    static std::shared_ptr<Dialog> Create(
        std::string title,
        std::shared_ptr<Widget> content = nullptr,
        std::string confirm_label = "OK",
        std::string cancel_label = ""
    );

    void SetTitle(std::string title);
    void SetContent(std::shared_ptr<Widget> content);
    void SetPanelWidth(float width);
    // Explicit panel height; 0 restores content-derived auto sizing.
    void SetPanelHeight(float height);
    void SetOnClosed(std::function<void()> callback);
    void Close();
    // True only when the dialog was closed via the confirm button or Enter.
    bool WasConfirmed() const { return confirmed_; }

    // ---- accept/reject semantics (QDialog) ----
    // Accept() records Result::Accepted, marks the dialog confirmed and
    // closes; Reject() records Result::Rejected and closes. Escape,
    // scrim click, the close affordance and Cancel all route through
    // Reject so every dismissal carries a code.
    void Accept();
    void Reject();
    // QDialog::setResult — stores the code without closing.
    void SetResult(Result result) { result_ = result; }
    Result GetResult() const { return result_; }
    // Fired once from Close() with the final result code.
    void SetOnResult(std::function<void(Result)> callback);

    // ---- custom button row (views::DialogDelegate::SetButtons) ----
    // Replaces the confirm/cancel pair while non-empty; clearing
    // restores the legacy labels. Buttons lay out right-aligned in the
    // footer in order.
    void SetButtons(std::vector<Button> buttons);
    const std::vector<Button>& GetButtons() const { return buttons_; }

    // ---- modality / dismissal ----
    // Modal (default) draws the blurred scrim and swallows all input
    // outside the panel. Modeless dialogs skip the scrim: outside clicks
    // and unrelated keys pass through to widgets behind the panel.
    void SetModal(bool modal);
    bool IsModal() const { return modal_; }
    void SetCloseOnEscape(bool close);
    bool GetCloseOnEscape() const { return close_on_escape_; }
    void SetCloseOnScrimClick(bool close);
    bool GetCloseOnScrimClick() const { return close_on_scrim_click_; }

    // ---- window chrome ----
    // Resizable adds a bottom-right grip that adjusts panel size;
    // movable lets the header strip drag the panel around (both
    // views::Widget window behaviors).
    void SetResizable(bool resizable);
    bool IsResizable() const { return resizable_; }
    void SetMovable(bool movable);
    bool IsMovable() const { return movable_; }
    // Widget focused when the dialog first appears in a tree
    // (views::DialogDelegate::SetInitiallyFocusedView).
    void SetInitialFocus(std::shared_ptr<Widget> widget);

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
    Layout::Rect GripRect(const Layout::Rect& panel) const;
    int HitCustomButton(const Layout::Point& position) const;
    float ButtonWidth(const std::string& label, ButtonStyle style) const;

    std::string title_;
    std::string confirm_label_;
    std::string cancel_label_;
    std::shared_ptr<Widget> content_;
    float panel_width_ = 420.0f;
    float panel_height_ = 0.0f;  // 0 = auto from content
    Layout::Point panel_offset_{};
    Layout::Rect close_rect_;
    Layout::Rect confirm_rect_;
    Layout::Rect cancel_rect_;
    std::vector<Layout::Rect> button_rects_;
    bool closed_ = false;
    bool confirmed_ = false;
    std::function<void()> on_closed_;

    std::vector<Button> buttons_;
    Result result_ = Result::None;
    std::function<void(Result)> on_result_;
    bool modal_ = true;
    bool close_on_escape_ = true;
    bool close_on_scrim_click_ = true;
    bool resizable_ = false;
    bool movable_ = false;
    std::shared_ptr<Widget> initial_focus_;
    bool initial_focus_applied_ = false;
    bool dragging_ = false;
    bool resizing_ = false;
    Layout::Point drag_anchor_{};
    Layout::Point resize_anchor_{};
    float resize_w_ = 0.0f;
    float resize_h_ = 0.0f;
};

} // namespace XER::UI::Widgets

#endif // XER_UI_WIDGETS_DIALOG_H
