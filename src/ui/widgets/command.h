#ifndef XER_UI_WIDGETS_COMMAND_H
#define XER_UI_WIDGETS_COMMAND_H

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "motion/motion.h"
#include "widgets/widget.h"

namespace XER::UI::Widgets {

// Command — searchable command palette (overlay): a centered panel with
// a filter input and a keyboard-navigable result list. Open via
// Widget::ShowOverlay() or the host calling OpenCommand-style APIs;
// items carry an action callback fired on Enter/click.
class Command final : public Widget {
public:
    struct Item {
        std::string label;
        std::string hint;      // right-aligned shortcut/group hint
        std::function<void()> action;
        bool enabled = true;
    };

    static std::shared_ptr<Command> Create(std::string placeholder = "Type a command…");

    void SetItems(std::vector<Item> items);
    void SetPlaceholder(std::string placeholder);
    void Open();
    void Close();
    bool IsOpen() const { return open_; }
    void SetOnOpenChange(std::function<void(bool)> cb);

    void Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) override;
    void Layout(const Layout::Rect& bounds) override;
    using Widget::Layout;
    void Tick(double dt_seconds) override;
    bool HandleEvent(const Event& event) override;

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

private:
    explicit Command(std::string placeholder);
    std::vector<int> Filtered() const;
    void Execute(int filtered_idx);
    Layout::Rect PanelRect(const Layout::Rect& b) const;
    Layout::Rect InputRect(const Layout::Rect& panel) const;
    Layout::Rect ListRect(const Layout::Rect& panel) const;

    std::vector<Item> items_;
    std::string query_;
    std::string placeholder_;
    bool open_ = false;
    int highlight_ = 0;
    float scroll_ = 0.0f;
    Motion::Tween open_t_;
    std::function<void(bool)> on_open_change_;
};

} // namespace XER::UI::Widgets

#endif // XER_UI_WIDGETS_COMMAND_H
