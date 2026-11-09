#ifndef XER_UI_WIDGETS_AVATAR_H
#define XER_UI_WIDGETS_AVATAR_H

#include <string>

#include "widgets/widget.h"

namespace XER::UI::Widgets {

// shadcn-style avatar: a rounded square/circle showing user initials on a
// deterministic background color derived from the name.
class Avatar final : public Widget {
public:
    Avatar(std::string name = {}, float size = 40.0f);

    void SetName(std::string name);
    void SetSize(float size);
    const std::string& GetName() const { return name_; }

    void Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) override;
    void Layout(const Layout::Rect& bounds) override;
    using Widget::Layout;
    bool HandleEvent(const Event& event) override;

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

private:
    std::string name_;
    float size_ = 40.0f;
};

} // namespace XER::UI::Widgets

#endif // XER_UI_WIDGETS_AVATAR_H
