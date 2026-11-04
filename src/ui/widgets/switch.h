#ifndef XER_UI_WIDGETS_SWITCH_H
#define XER_UI_WIDGETS_SWITCH_H

#include <functional>

#include "widgets/widget.h"

namespace XER::UI::Widgets {

class Switch final : public Widget {
public:
    explicit Switch(bool checked = false);

    bool IsChecked() const;
    void SetChecked(bool checked);
    void SetOnChanged(std::function<void(bool)> callback);

    void Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) override;
    void Layout(const Layout::Rect& bounds) override;
    bool HandleEvent(const Event& event) override;

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

private:
    void Toggle();

    bool checked_ = false;
    std::function<void(bool)> on_changed_;
};

} // namespace XER::UI::Widgets

#endif // XER_UI_WIDGETS_SWITCH_H
