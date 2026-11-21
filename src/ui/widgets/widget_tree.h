#ifndef XER_UI_WIDGETS_WIDGET_TREE_H
#define XER_UI_WIDGETS_WIDGET_TREE_H

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "layout/geometry.h"
#include "theme/tokens.h"
#include "widgets/widget.h"

namespace XER::UI::Theme {
struct Tokens;
}

namespace XER::UI::Rendering {
class Renderer;
}

namespace XER::UI::Core {
class UiEventBus;
}

namespace XER::UI::CSS {
class WidgetStyleHost;
}

namespace XER::UI::Widgets {

// 2x3 affine matrix (a,b,c,d,tx,ty — the Renderer::SetTransform
// layout). One frame's render traversal keeps a stack of these so a
// child's CSS transform composes onto its parent's matrix instead of
// replacing it.
struct RenderTransform {
    float a = 1.0f, b = 0.0f, c = 0.0f, d = 1.0f;
    float tx = 0.0f, ty = 0.0f;
};

class WidgetTree {
public:
    void SetRoot(std::shared_ptr<Widget> root);
    std::shared_ptr<Widget> GetRoot() const;

    // Overlay layer: a widget rendered above the main tree and hit-tested
    // first. Used by popup-style widgets (Select dropdowns, Dialogs, menus,
    // tooltips). The overlay is laid out to the same bounds as the root.
    void ShowOverlay(std::shared_ptr<Widget> overlay);
    void HideOverlay();
    std::shared_ptr<Widget> GetOverlay() const { return overlay_; }
    void Layout(const Layout::Rect& bounds);
    void Layout(const Layout::Rect& bounds, const Theme::Tokens& tokens);
    // `damage` (optional, client coords): pixels that must be repainted.
    // Renderers get it via SetFrameDamage and the traversal culls widgets
    // fully outside it; nullptr means full repaint.
    void Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens,
                const Layout::Rect* damage = nullptr) const;

    // Diagnostics for the last Render() pass (perf HUD / profiling):
    // `painted` = widgets whose Render() ran, `culled` = widgets skipped
    // entirely by the damage cull. Zeroed at the start of every Render.
    struct FrameWidgetStats {
        std::uint32_t painted = 0;
        std::uint32_t culled = 0;
    };
    FrameWidgetStats LastFrameWidgetStats() const {
        return frame_widget_stats_;
    }
    bool HandleEvent(const Event& event);

    // Event bus shared with the owning Window. When set, every incoming
    // event passes through the bus's filter chain (which may mutate or
    // swallow it) and is then published to bus listeners before the tree
    // dispatches it to widgets. Not owned.
    void SetEventBus(Core::UiEventBus* bus) { event_bus_ = bus; }
    Core::UiEventBus* GetEventBus() const { return event_bus_; }

    // Per-frame tick for tree-level animation state (hover tooltips). Widget
    // subtrees are ticked separately by the host via Widget::TickTree.
    void Tick(double dt_seconds);

    // Returns true once if any widget requested a repaint since the previous
    // call, clearing the dirty flags. Used by the host to skip redundant paints.
    bool ConsumeDirtyPaint();
    // Same for layout requests raised through Widget::RequestLayout().
    bool ConsumeLayoutRequest();

    // --- Damage-region tracking (partial repaints) -------------------------
    // Widgets report the bounds they invalidated; the union becomes the
    // next InvalidateRect region instead of the whole client area.
    // Coordinates are in widget (DIP) space.
    void AddDamageRect(Layout::Rect rect);
    // Returns the accumulated damage and clears it. `valid` is false when
    // nothing was reported — callers should treat that as "repaint all".
    struct DamageRect {
        Layout::Rect rect{};
        bool valid = false;
    };
    DamageRect ConsumeDamageRect();
    // Structural changes (root/overlay swap) that can affect pixels outside
    // any widget's recorded bounds — forces a full repaint once.
    void RequestFullDamage() { full_damage_pending_ = true; }
    bool ConsumeFullDamage() {
        const bool v = full_damage_pending_;
        full_damage_pending_ = false;
        return v;
    }
    // True while WidgetTree::Layout runs — Invalidate/SetBounds skip
    // damage recording because a layout request already forces a full
    // repaint.
    bool LayoutInProgress() const { return layout_in_progress_; }

    // --- CSS styling (XER::Content::CSS bridge) ----------------------
    // Attaches a WidgetStyleHost mirroring this tree; Update runs
    // automatically at the top of Layout() and Render(), so widget
    // computed styles are always current when they paint or size.
    void SetStyleHost(std::shared_ptr<CSS::WidgetStyleHost> host);
    CSS::WidgetStyleHost* GetStyleHost() const { return style_host_.get(); }
    // Convenience: creates the host on first call and sets/appends a
    // stylesheet parsed from CSS text.
    void SetStyleSheet(std::string_view css_text);
    void AddStyleSheet(std::string_view css_text);
    // Clears the hovered widget (e.g. on WM_MOUSELEAVE).
    void ClearHover();
    // Drops left-button mouse capture without a MouseUp (e.g. the OS
    // released capture on focus loss). A synthetic MouseUp is delivered to
    // the captured widget first so drag state (sliders, scrollbars,
    // selections) settles instead of sticking forever.
    void CancelPointerCapture();

    // Fallback handler for right-button MouseDown events that no widget
    // consumed (returns true = handled). Used for context menus.
    void SetContextMenuHandler(std::function<bool(const Event&)> handler) {
        context_menu_handler_ = std::move(handler);
    }

    // Fallback handler for key/text events no focused widget or overlay
    // consumed (returns true = handled). Used for global shortcuts.
    void SetKeyHandler(std::function<bool(const Event&)> handler) {
        key_handler_ = std::move(handler);
    }

private:
    void ClearFocus(const std::shared_ptr<Widget>& widget);
    void ClearPaintDirty(const std::shared_ptr<Widget>& widget);
    void ClearLayoutDirty(const std::shared_ptr<Widget>& widget);
    void UpdateHover(const std::shared_ptr<Widget>& target);
    void HideTooltip();
    void ShowTooltipFor(const std::shared_ptr<Widget>& target);
    void RenderSubtree(
        const std::shared_ptr<Widget>& widget,
        Rendering::Renderer& renderer,
        const Theme::Tokens& tokens,
        const Layout::Rect* cull,
        std::vector<RenderTransform>* transform_stack
    ) const;
    void CollectFocusable(
        const std::shared_ptr<Widget>& widget,
        bool ancestors_available,
        std::vector<std::shared_ptr<Widget>>& widgets
    ) const;
    std::shared_ptr<Widget> FindHitTarget(
        const std::shared_ptr<Widget>& widget,
        const Layout::Point& position,
        bool ancestors_available
    ) const;
    // Click dispatch with parent-chain bubbling (same rule MouseWheel
    // already uses): the event walks from `target` toward the root until
    // a widget consumes it. Shared by the real Click path and the
    // MouseUp-synthesized click so both behave identically.
    bool DispatchClick(
        const std::shared_ptr<Widget>& target,
        const Event& event
    );
    bool MoveFocus(bool backwards);
    void SetFocusedWidget(const std::shared_ptr<Widget>& widget);

    std::shared_ptr<Widget> root_;
    std::shared_ptr<Widget> overlay_;
    // Passive layer (tooltips): rendered above the root but never
    // hit-tested, so it can't steal input from the widget underneath.
    std::shared_ptr<Widget> passive_overlay_;
    std::weak_ptr<Widget> tooltip_target_;
    double hover_time_ = 0.0;
    bool tooltip_shown_ = false;
    Layout::Rect last_bounds_;
    // Last tokens passed to Layout(bounds, tokens); reused when overlays
    // and tooltips are shown between relayouts.
    Theme::Tokens last_tokens_{};
    Layout::Point last_pointer_position_{};
    std::weak_ptr<Widget> left_mouse_down_target_;
    std::weak_ptr<Widget> synthesized_click_target_;
    std::weak_ptr<Widget> hovered_widget_;
    Layout::Point synthesized_click_position_;
    bool suppress_duplicate_click_ = false;
    // Union of invalidated widget bounds since the last ConsumeDamageRect.
    Layout::Rect pending_damage_{};
    bool pending_damage_valid_ = false;
    bool full_damage_pending_ = false;
    bool layout_in_progress_ = false;
    std::function<bool(const Event&)> context_menu_handler_;
    std::function<bool(const Event&)> key_handler_;
    Core::UiEventBus* event_bus_ = nullptr;
    // mutable: Render() is const but style resolution is internal
    // bookkeeping — it writes ComputedStyle onto widgets.
    mutable std::shared_ptr<CSS::WidgetStyleHost> style_host_;
    // mutable for the same reason: Render() is const but per-frame
    // counters are bookkeeping, reset at the top of every Render().
    mutable FrameWidgetStats frame_widget_stats_;
};

} // namespace XER::UI::Widgets

#endif // XER_UI_WIDGETS_WIDGET_TREE_H
