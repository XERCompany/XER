#ifndef XER_UI_WIDGETS_HOVER_CARD_H
#define XER_UI_WIDGETS_HOVER_CARD_H

#include <functional>
#include <memory>
#include <string>

#include "widgets/widget.h"

namespace XER::UI::Widgets {

// HoverCard — shadcn-style hover card: a delayed floating panel anchored
// to this widget showing a title, optional description and optional
// content widget. Opens after `open_delay` seconds of hover; closes when
// the pointer leaves (with a short grace so diagonal moves don't flicker).
// Falls back to nothing when detached (no WidgetTree).
class HoverCard final : public Widget {
public:
    HoverCard();
    ~HoverCard() override;

    // Trigger widget shown in the normal tree; typically wrap your control.
    void SetTrigger(std::shared_ptr<Widget> trigger);
    void SetTitle(std::string title);
    const std::string& GetTitle() const { return title_; }
    void SetDescription(std::string description);
    const std::string& GetDescription() const { return description_; }
    void SetContent(std::shared_ptr<Widget> content);
    void SetOpenDelay(float seconds);
    float GetOpenDelay() const { return open_delay_; }
    void SetOnAction(std::function<void()> callback);
    void SetActionLabel(std::string label);

    void Render(Rendering::Renderer&, const Theme::Tokens&) override {}
    void Layout(const Layout::Rect& bounds) override;
    using Widget::Layout;
    bool HandleEvent(const Event&) override { return false; }
    void Tick(double dt_seconds) override;

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

private:
    void OpenCard();
    void CloseCard();
    bool IsOpen() const;

    std::shared_ptr<Widget> trigger_;
    std::string title_;
    std::string description_;
    std::shared_ptr<Widget> content_;
    std::string action_label_;
    float open_delay_ = 0.5f;
    float hover_time_ = 0.0f;
    bool was_hovered_ = false;
    float close_grace_ = 0.0f;
    std::weak_ptr<Widget> card_;
    std::function<void()> on_action_;
};

} // namespace XER::UI::Widgets

#endif // XER_UI_WIDGETS_HOVER_CARD_H
