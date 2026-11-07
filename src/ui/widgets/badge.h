#ifndef XER_UI_WIDGETS_BADGE_H
#define XER_UI_WIDGETS_BADGE_H

#include <string>

#include "widgets/widget.h"

namespace XER::UI::Widgets {

enum class BadgeVariant {
    Default,
    Secondary,
    Outline,
    Accent
};

class Badge final : public Widget {
public:
    explicit Badge(std::string text = {}, BadgeVariant variant = BadgeVariant::Default);

    void SetText(std::string text);
    const std::string& GetText() const;
    void SetVariant(BadgeVariant variant);
    BadgeVariant GetVariant() const;

    void Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) override;
    void Layout(const Layout::Rect& bounds) override;
    bool HandleEvent(const Event& event) override;

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

private:
    std::string text_;
    BadgeVariant variant_ = BadgeVariant::Default;
};

} // namespace XER::UI::Widgets

#endif // XER_UI_WIDGETS_BADGE_H
