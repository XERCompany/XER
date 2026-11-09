#ifndef XER_UI_WIDGETS_BUTTON_H
#define XER_UI_WIDGETS_BUTTON_H

#include <functional>
#include <string>

#include "motion/motion.h"
#include "widgets/widget.h"

namespace XER::UI::Widgets {

enum class ButtonStyle {
    Primary,
    Secondary,
    Outline,
    // shadcn "ghost": transparent until hover.
    Ghost,
    // shadcn "destructive": danger fill for irreversible actions.
    Destructive
};

class Button final : public Widget {
public:
    explicit Button(std::string text = {}, ButtonStyle style = ButtonStyle::Secondary);

    void SetText(std::string text);
    const std::string& GetText() const;
    void SetOnClick(std::function<void()> callback);
    void SetStyle(ButtonStyle style);
    // Busy state: draws a spinner inside the button, keeps its width and
    // swallows clicks until cleared.
    void SetLoading(bool loading);
    bool IsLoading() const { return loading_; }

    void Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) override;
    void Layout(const Layout::Rect& bounds) override;
    using Widget::Layout;
    void Tick(double dt_seconds) override;
    bool HandleEvent(const Event& event) override;

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

private:
    void Activate();

    std::string text_;
    std::function<void()> on_click_;
    ButtonStyle style_;
    bool loading_ = false;

    // Motion state (springs/tweens per the XER motion rules).
    Motion::Spring press_;       // 0..1 while the pointer is held down
    Motion::Tween hover_t_;      // eased hover wash
    float spinner_angle_ = 0.0f; // loading spinner rotation
    bool held_ = false;
};

} // namespace XER::UI::Widgets

#endif // XER_UI_WIDGETS_BUTTON_H
