#ifndef XER_UI_WIDGETS_POPOVER_H
#define XER_UI_WIDGETS_POPOVER_H

#include <memory>
#include <string>

#include "widgets/widget.h"

namespace XER::UI::Widgets {

// Popover — click-toggled floating panel anchored to this widget (or an
// explicit anchor rect). Shows `content` in the overlay layer; closes on
// outside click, Escape, or Close(). Align/side select placement.
class Popover final : public Widget {
public:
    enum class Side { Bottom, Top, Left, Right };
    enum class Align { Start, Center, End };

    Popover();
    ~Popover() override;

    void SetTrigger(std::shared_ptr<Widget> trigger);
    void SetContent(std::shared_ptr<Widget> content);
    void SetTitle(std::string title);
    const std::string& GetTitle() const { return title_; }
    void SetSide(Side side);
    Side GetSide() const { return side_; }
    void SetAlign(Align align);
    Align GetAlign() const { return align_; }
    void SetOpen(bool open);
    bool IsOpen() const;
    void Open();
    void Close();
    void SetOnOpenChange(std::function<void(bool)> callback);

    void Render(Rendering::Renderer&, const Theme::Tokens&) override {}
    void Layout(const Layout::Rect& bounds) override;
    using Widget::Layout;
    bool HandleEvent(const Event&) override { return false; }

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

private:
    void SyncOpen(bool open);

    std::shared_ptr<Widget> trigger_;
    std::shared_ptr<Widget> content_;
    std::string title_;
    Side side_ = Side::Bottom;
    Align align_ = Align::Start;
    bool open_ = false;
    std::weak_ptr<Widget> panel_;
    std::function<void(bool)> on_open_change_;
};

} // namespace XER::UI::Widgets

#endif // XER_UI_WIDGETS_POPOVER_H
