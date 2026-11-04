#ifndef XER_UI_WIDGETS_BUTTON_H
#define XER_UI_WIDGETS_BUTTON_H

#include <functional>
#include <string>

#include "widgets/widget.h"

namespace XER::UI::Widgets {

class Button final : public Widget {
public:
    explicit Button(std::string text = {});

    void SetText(std::string text);
    const std::string& GetText() const;
    void SetOnClick(std::function<void()> callback);

    void Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) override;
    void Layout(const Layout::Rect& bounds) override;
    bool HandleEvent(const Event& event) override;

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

private:
    void Activate();

    std::string text_;
    std::function<void()> on_click_;
};

} // namespace XER::UI::Widgets

#endif // XER_UI_WIDGETS_BUTTON_H
