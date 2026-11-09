#ifndef XER_UI_WIDGETS_TABS_H
#define XER_UI_WIDGETS_TABS_H

#include <functional>
#include <string>
#include <vector>

#include "motion/motion.h"
#include "widgets/widget.h"

namespace XER::UI::Widgets {

// shadcn-style tabs: a row of tab triggers with an animated active
// indicator. Two styles:
//   Pill      — segmented-control look: a rounded chip that slides under
//               the active label (used by the app nav).
//   Underline — classic shadcn tabs: a 2px bar that glides along the bottom.
// The widget only renders the tab bar; panels are managed by the owner via
// GetActiveIndex()/SetActiveIndex() (typically swapped as siblings).
// Keyboard: when focused, ←/→ move between tabs, Home/End jump to ends.
class Tabs final : public Widget {
public:
    enum class Style { Pill, Underline };

    Tabs();

    void AddTab(std::string label);
    void ClearTabs();
    int GetActiveIndex() const { return active_index_; }
    void SetActiveIndex(int index);
    void SetOnChanged(std::function<void(int)> callback);
    void SetStyle(Style style);
    Style GetStyle() const { return style_; }

    void Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) override;
    void Layout(const Layout::Rect& bounds) override;
    using Widget::Layout;
    void Tick(double dt_seconds) override;
    bool HandleEvent(const Event& event) override;

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

private:
    int HitTab(const Layout::Point& position) const;
    void RetargetIndicator();
    void ActivateTab(int index);
    // Pill geometry for the active indicator (inset inside the tab cell).
    Layout::Rect PillRect(float x, float w, const Layout::Rect& bounds) const;

    std::vector<std::string> labels_;
    std::vector<Layout::Rect> tab_rects_;
    int active_index_ = -1;
    int hovered_tab_ = -1;
    int pressed_tab_ = -1;
    std::function<void(int)> on_changed_;
    Style style_ = Style::Pill;

    // Sliding active indicator (springs chase the active tab's rect).
    Motion::Spring ind_x_;
    Motion::Spring ind_w_;
    Motion::Tween press_t_;   // 0..1 press feedback on the held tab
    bool indicator_init_ = false;
};

} // namespace XER::UI::Widgets

#endif // XER_UI_WIDGETS_TABS_H
