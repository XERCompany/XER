#ifndef XER_UI_WIDGETS_PIE_MENU_H
#define XER_UI_WIDGETS_PIE_MENU_H

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "motion/motion.h"
#include "widgets/widget.h"

namespace XER::UI::Widgets {

// Radial "marking menu" (piemenu.jpmarqu.es model) shown in the overlay
// layer: items fan out as wedges around a center point. The canonical
// gesture is press → drag out of the dead zone → release to select;
// releasing inside the dead zone cancels, as do Escape and clicks
// outside the ring. Keyboard fallback: arrows aim at a wedge by
// direction, Enter/Space activates the highlighted item. Items may be
// replaced while open (SetItems) — the ring re-flows and keeps the
// highlight.
//
// Usage mirrors ContextMenu: Create(), SetCenter() at the cursor, then
// someWidget->ShowOverlay(menu). When the same press that opened the
// menu continues as the drag (marking-menu style), open on a
// non-capturing gesture (right-button press / context-menu handler) or
// call ShowOverlay during MouseDown handling BEFORE the tree latches
// left-button capture — left-capture stays with the pre-overlay target
// (WidgetTree routes held-button moves to the MouseDown target), so a
// left-press-opened menu will not see the drag. Right-drag and
// click-then-click work for any open path.
class PieMenu final : public Widget {
public:
    struct Item {
        std::string label;
        // Logical icon name drawn above the label through the shared
        // IconResolver (same signature as Button::IconResolver; install
        // the same host resolver with SetIconResolver). Empty keeps the
        // wedge text-only.
        std::string icon;
        std::function<void()> action;
        bool enabled = true;
        // Destructive action: danger label + danger-tinted wedge.
        bool danger = false;
    };

    static std::shared_ptr<PieMenu> Create(std::vector<Item> items = {});

    // Same contract as Button::SetIconResolver: a process-wide lookup
    // the host installs once (the video editor wires its raster icon
    // pack). Unset => every wedge is text-only.
    using IconResolver =
        std::function<bool(Rendering::Renderer& renderer,
                           const char* icon_name, int px,
                           Theme::Color tint, Layout::Rect box)>;
    static void SetIconResolver(IconResolver resolver);

    void SetItems(std::vector<Item> items);
    // Menu hub in client coordinates (usually the cursor position at
    // the opening press). Clamped so the ring stays on screen.
    void SetCenter(Layout::Point position);
    Layout::Point Center() const { return center_; }
    // Outer wedge radius in DIP — the piemenu `radius` prop (96).
    void SetRadius(float radius);
    float Radius() const { return radius_; }
    // Cancel zone radius in DIP — the piemenu `deadZone` prop (20).
    // Inside it nothing highlights and release cancels.
    void SetDeadZone(float radius);
    float DeadZone() const { return dead_zone_; }

    // Fired once with the chosen item index, in addition to the item's
    // own action.
    void SetOnSelect(std::function<void(int)> callback);
    // Fired whenever the highlighted wedge changes (-1 = none — the
    // pointer is inside the dead zone or nothing has been aimed at).
    void SetOnHighlight(std::function<void(int)> callback);
    // Fired on user-initiated cancels (Escape, dead-zone release,
    // click outside the ring). Programmatic Close() does not fire it.
    void SetOnCancel(std::function<void()> callback);

    int HighlightedIndex() const { return highlighted_; }
    // Dismisses the menu without selecting — the ContextMenu::Close
    // contract (only hides the tree's overlay when it is still us).
    void Close();

    void Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) override;
    void Layout(const Layout::Rect& bounds) override;
    using Widget::Layout;
    void Tick(double dt_seconds) override;
    bool HandleEvent(const Event& event) override;

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

private:
    explicit PieMenu(std::vector<Item> items);
    // Effective (screen-clamped) hub and radii for the current bounds.
    Layout::Point Hub() const;
    float RingOuter() const;
    // Wedge index under `position`: -1 outside the ring, -2 inside the
    // dead zone. Angle is measured from the hub; item 0 sits at the top.
    int HitWedge(const Layout::Point& position) const;
    void SetHighlighted(int index);
    // Ends the gesture: commits `index` when >= 0 and enabled, else cancels.
    void Finish(int index);
    void Cancel();
    void MoveHighlightByDirection(float angle_radians);
    float ItemAngle(int index) const;

    std::vector<Item> items_;
    Layout::Point center_;
    float radius_ = 96.0f;
    float dead_zone_ = 20.0f;
    int highlighted_ = -1;
    // Selected item kept for one frame of post-select paint (piemenu's
    // data-selected); exits are instant, so this only marks intent.
    int selected_ = -1;
    bool tracking_ = false;       // a press is held — release commits
    bool suppress_click_ = false; // swallow the synthesized click post-drag
    bool closed_ = false;
    bool ticked_ = false;
    bool rendered_ = false;
    Motion::Tween open_t_;
    // Hub indicator angle easing toward the highlighted wedge
    // (PieMenuIndicator: turns the short way).
    float indicator_angle_ = 0.0f;
    float indicator_target_ = 0.0f;
    bool indicator_has_target_ = false;
    std::function<void(int)> on_select_;
    std::function<void(int)> on_highlight_;
    std::function<void()> on_cancel_;
};

} // namespace XER::UI::Widgets

#endif // XER_UI_WIDGETS_PIE_MENU_H
