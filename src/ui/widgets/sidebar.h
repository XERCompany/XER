#ifndef XER_UI_WIDGETS_SIDEBAR_H
#define XER_UI_WIDGETS_SIDEBAR_H

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "motion/motion.h"
#include "widgets/widget.h"

namespace XER::UI::Widgets {

// Sidebar — vertical navigation rail: header slot, icon+label items,
// optional section separators, active-item highlight, collapse toggle.
class Sidebar final : public Widget {
public:
    struct Item {
        std::string label;
        std::string icon;      // short glyph/text shown left of label
        bool enabled = true;
        bool separator_before = false;
    };

    static std::shared_ptr<Sidebar> Create(std::string header = {});

    void SetHeader(std::string header);
    void SetItems(std::vector<Item> items);
    void SetActiveIndex(int index);   // -1 = none
    int GetActiveIndex() const { return active_; }
    void SetCollapsed(bool collapsed);
    bool IsCollapsed() const { return collapsed_; }
    void SetOnSelect(std::function<void(int)> cb);

    void Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) override;
    void Layout(const Layout::Rect& bounds) override;
    using Widget::Layout;
    void Tick(double dt_seconds) override;
    bool HandleEvent(const Event& event) override;

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

private:
    explicit Sidebar(std::string header);
    int ItemAtY(float y, const Layout::Rect& b) const;
    float ItemHeight() const;

    std::string header_;
    std::vector<Item> items_;
    int active_ = -1;
    int hovered_ = -1;
    bool collapsed_ = false;
    Motion::Tween collapse_t_;
    float collapse_ = 0.0f;   // 0 = expanded, 1 = collapsed
    std::function<void(int)> on_select_;
};

} // namespace XER::UI::Widgets

#endif // XER_UI_WIDGETS_SIDEBAR_H
