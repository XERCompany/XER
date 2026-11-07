#ifndef XER_UI_WIDGETS_ALERT_H
#define XER_UI_WIDGETS_ALERT_H

#include <string>

#include "widgets/widget.h"

namespace XER::UI::Widgets {

// shadcn-style alert: an inline banner with a title, optional description
// and a leading status glyph. Variants: default / destructive.
class Alert final : public Widget {
public:
    enum class Variant { Default, Destructive };

    Alert(std::string title = {}, std::string description = {}, Variant variant = Variant::Default);

    void SetTitle(std::string title);
    void SetDescription(std::string description);
    void SetVariant(Variant variant);

    void Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) override;
    void Layout(const Layout::Rect& bounds) override;
    bool HandleEvent(const Event& event) override;

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

private:
    std::string title_;
    std::string description_;
    Variant variant_ = Variant::Default;
};

} // namespace XER::UI::Widgets

#endif // XER_UI_WIDGETS_ALERT_H
