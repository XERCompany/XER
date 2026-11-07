#ifndef XER_UI_WIDGETS_CONTEXT_MENU_H
#define XER_UI_WIDGETS_CONTEXT_MENU_H

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "motion/motion.h"
#include "widgets/widget.h"

namespace XER::UI::Widgets {

// shadcn-style context/dropdown menu shown in the overlay layer: a vertical
// list of actionable items with hover highlight. Create via
// ContextMenu::Create(), position with SetAnchor(), then Widget::ShowOverlay().
class ContextMenu final : public Widget {
public:
    struct Item {
        std::string label;
        std::function<void()> action;
        bool enabled = true;
        bool separator_before = false;
        // shadcn-style extras: a check glyph on the left and a right-aligned
        // keyboard hint, both muted/accent like the real menu items.
        bool checked = false;
        std::string shortcut;
        // Destructive action: red label + red-tinted hover (shadcn danger).
        bool danger = false;
    };

    static std::shared_ptr<ContextMenu> Create(std::vector<Item> items);

    void SetAnchor(Layout::Point position);
    void Close();

    void Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) override;
    void Layout(const Layout::Rect& bounds) override;
    void Tick(double dt_seconds) override;
    bool HandleEvent(const Event& event) override;

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

private:
    explicit ContextMenu(std::vector<Item> items);
    Layout::Rect MenuRect(const Layout::Rect& bounds) const;
    int HitItem(const Layout::Point& position) const;
    float ScrollRange() const;
    void ScrollTo(float y);
    void ScrollFromPointer(float y, bool grab_offset, float grab);
    Layout::Rect ScrollbarThumbRect() const;
    bool ScrollbarZoneContains(const Layout::Point& position) const;
    void EnsureItemVisible(int index);

    std::vector<Item> items_;
    // Item rects in unscrolled content coordinates (origin = menu top).
    std::vector<Layout::Rect> item_rects_;
    // Cached geometry derived from items_ + overlay bounds inside MenuRect;
    // mutable because MenuRect is const.
    mutable float content_height_ = 0.0f;
    mutable float viewport_height_ = 0.0f;
    float scroll_y_ = 0.0f;
    bool scrollbar_dragging_ = false;
    float scrollbar_grab_ = 0.0f;
    bool suppress_click_ = false;
    Layout::Point anchor_;
    int hovered_item_ = -1;
    bool closed_ = false;
    Motion::Tween open_t_;
};

} // namespace XER::UI::Widgets

#endif // XER_UI_WIDGETS_CONTEXT_MENU_H
