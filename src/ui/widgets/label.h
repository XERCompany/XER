#ifndef XER_UI_WIDGETS_LABEL_H
#define XER_UI_WIDGETS_LABEL_H

#include <string>

#include "widgets/widget.h"

namespace XER::UI::Widgets {

class Label final : public Widget {
public:
    explicit Label(std::string text = {}, bool muted = false);

    void SetText(std::string text);
    const std::string& GetText() const;
    void SetMuted(bool muted);
    bool IsMuted() const;

    void Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) override;
    void Layout(const Layout::Rect& bounds) override;
    bool HandleEvent(const Event& event) override;

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

private:
    std::string text_;
    bool muted_ = false;
};

} // namespace XER::UI::Widgets

#endif // XER_UI_WIDGETS_LABEL_H
