#ifndef XER_UI_WIDGETS_SHEET_H
#define XER_UI_WIDGETS_SHEET_H

#include <functional>
#include <memory>
#include <string>

#include "widgets/widget.h"

namespace XER::UI::Widgets {

// Sheet — modal panel sliding in from a screen edge (left/right/top/
// bottom), like Dialog but edge-anchored with a scrim. Shown via
// Widget::ShowOverlay(); closes on scrim click, Escape, or Close().
class Sheet final : public Widget {
public:
    enum class Side { Left, Right, Top, Bottom };

    static std::shared_ptr<Sheet> Create(std::string title = {},
                                         std::shared_ptr<Widget> content = {},
                                         Side side = Side::Right);

    void SetTitle(std::string title);
    void SetContent(std::shared_ptr<Widget> content);
    void SetSide(Side side);
    Side GetSide() const { return side_; }
    void SetPanelSize(float size);
    void SetOnClosed(std::function<void()> callback);
    void Close();
    bool WasDismissed() const { return dismissed_; }

    void Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) override;
    void Layout(const Layout::Rect& bounds) override;
    using Widget::Layout;
    void Tick(double dt_seconds) override;
    bool HandleEvent(const Event& event) override;

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

private:
    explicit Sheet(std::string title, std::shared_ptr<Widget> content,
                   Side side);
    Layout::Rect PanelRect(const Layout::Rect& bounds) const;

    std::string title_;
    std::shared_ptr<Widget> content_;
    Side side_ = Side::Right;
    float panel_size_ = 420.0f;
    Layout::Rect close_rect_{};
    bool closed_ = false;
    bool dismissed_ = false;
    bool ticked_ = false;
    bool rendered_ = false;
    Motion::Tween open_t_;
    std::function<void()> on_closed_;
};

} // namespace XER::UI::Widgets

#endif // XER_UI_WIDGETS_SHEET_H
