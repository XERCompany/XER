#ifndef XER_UI_WIDGETS_WIDGET_H
#define XER_UI_WIDGETS_WIDGET_H

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "layout/geometry.h"
#include "theme/tokens.h"

namespace XER::UI {

namespace Rendering {
class Renderer;
struct TextStyle;
}

namespace Widgets {

class WidgetTree;

enum class KeyCode {
    None,
    Tab,
    Enter,
    Space,
    Backspace,
    Delete,
    ArrowLeft,
    ArrowRight,
    ArrowUp,
    ArrowDown,
    Home,
    End,
    PageUp,
    PageDown,
    Escape,
    // Letter keys are reported only while Ctrl is held (editing shortcuts).
    A,
    C,
    V,
    X,
    Y,
    Z
};

enum class MouseButton {
    None,
    Left,
    Middle,
    Right
};

struct Event {
    enum class Type {
        MouseMove,
        MouseDown,
        MouseUp,
        Click,
        MouseWheel,
        KeyDown,
        KeyUp,
        TextInput,
        Focus,
        Blur
    };

    Type type = Type::MouseMove;
    Layout::Point position;
    MouseButton mouse_button = MouseButton::None;
    KeyCode key = KeyCode::None;
    std::string text;
    // MouseWheel: signed delta in WHEEL_DELTA units (120 = one notch).
    int wheel_delta = 0;
    bool is_ctrl = false;
    bool is_shift = false;
    bool is_alt = false;
};

class Widget : public std::enable_shared_from_this<Widget> {
public:
    virtual ~Widget() = default;

    virtual void Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) = 0;
    virtual void Layout(const Layout::Rect& bounds) = 0;
    // Theme-aware layout. Widgets that need theme metrics (e.g. Card padding)
    // override this; the default delegates to Layout(bounds) so widgets that
    // ignore theme metrics keep working unchanged.
    virtual void Layout(const Layout::Rect& bounds, const Theme::Tokens& tokens);
    virtual bool HandleEvent(const Event& event) = 0;

    virtual Layout::Size GetMinSize() const = 0;
    virtual Layout::Size GetPreferredSize() const = 0;
    virtual Layout::Size GetMaxSize() const = 0;
    // Total height of scrollable content in local coordinates. ScrollArea
    // queries this to size its viewport; defaults to the preferred height.
    virtual float GetContentHeight() const { return GetPreferredSize().height; }

    // Frame tick driven by the host (Window frame callback). Override to
    // advance motion state; call Invalidate() while the widget is moving so
    // the next frame repaints. dt_seconds is wall time since the last tick.
    virtual void Tick(double dt_seconds) { (void)dt_seconds; }
    // Ticks this widget and every descendant. Invisible widgets are skipped.
    void TickTree(double dt_seconds);

    void SetVisible(bool visible);
    bool IsVisible() const;

    void SetEnabled(bool enabled);
    bool IsEnabled() const;

    void SetFocusable(bool focusable);
    bool IsFocusable() const;

    void SetFocused(bool focused);
    bool IsFocused() const;

    void SetHovered(bool hovered);
    bool IsHovered() const;

    // shadcn-style tooltip: text shown in a floating bubble after a short
    // hover delay (timing + positioning managed by WidgetTree). Empty
    // string disables the tooltip for this widget.
    void SetTooltip(std::string text);
    const std::string& GetTooltip() const { return tooltip_text_; }

    // True when this widget and every ancestor are visible. Animators use
    // this to sleep while their subtree is hidden (e.g. inactive tabs).
    bool IsEffectivelyVisible() const;

    // When true, descendants are clipped to this widget's bounds for both
    // rendering and hit-testing. Scroll containers set this so off-screen
    // content neither paints nor receives input.
    void SetClipsChildren(bool clips);
    bool ClipsChildren() const;

    // The visible viewport in absolute client coordinates, set by scroll
    // containers so widgets hosting non-clipped surfaces (e.g. child HWND
    // plug-views) can keep themselves inside the visible area.
    // Defaults to the widget's own bounds when unset.
    void SetVisibleRect(const Layout::Rect& rect);
    Layout::Rect GetVisibleRect() const;

    void SetParent(std::shared_ptr<Widget> parent);
    std::shared_ptr<Widget> GetParent() const;

    // The WidgetTree this widget belongs to (set when the tree adopts it as
    // root or overlay). Used by popup-style widgets (Select, Dialog, menus)
    // to show/hide overlay content.
    WidgetTree* GetTree() const { return tree_; }
    // Shows `overlay` in an overlay layer rendered above and event-priority
    // over the main tree. Pass nullptr to clear.
    void ShowOverlay(std::shared_ptr<Widget> overlay);
    void HideOverlay();

    void AddChild(std::shared_ptr<Widget> child);
    void RemoveChild(std::shared_ptr<Widget> child);
    const std::vector<std::shared_ptr<Widget>>& GetChildren() const;

protected:
    Widget() = default;

    Layout::Rect GetBounds() const;
    void SetBounds(const Layout::Rect& bounds);

    void Invalidate();
    void RequestLayout();

    // Draws the shadcn-style outer focus ring around the widget bounds when
    // the widget is focused. `radius` is the widget's own corner radius.
    void RenderFocusRing(
        Rendering::Renderer& renderer,
        const Theme::Tokens& tokens,
        float radius
    ) const;

    // Theme tokens captured by the most recent Layout(bounds, tokens)
    // call. Containers that implement only the legacy one-arg Layout use
    // CachedLayoutTokens() to keep forwarding theme metrics to children;
    // overrides of the two-arg form should call CacheLayoutTokens(tokens)
    // so the cache stays fresh.
    const Theme::Tokens& CachedLayoutTokens() const { return cached_tokens_; }
    void CacheLayoutTokens(const Theme::Tokens& tokens) { cached_tokens_ = tokens; }

    // Returns a rect inside the widget bounds with the text vertically
    // centered and `horizontal_padding` applied on both sides.
    Layout::Rect CenteredTextRect(
        Rendering::Renderer& renderer,
        std::string_view text,
        const Rendering::TextStyle& style,
        float horizontal_padding
    ) const;

private:
    friend class WidgetTree;

    void ClearSubtreeInteractionState();
    void SetTreeRecursive(WidgetTree* tree);
    // Registers children that were AddChild()ed while the widget was still
    // under construction (shared ownership not yet established, so
    // weak_from_this() was empty). Called from SetTreeRecursive, i.e. as soon
    // as the widget joins a WidgetTree.
    void FlushPendingChildren();

    Layout::Rect bounds_;
    Theme::Tokens cached_tokens_{};
    bool visible_ = true;
    bool enabled_ = true;
    bool focusable_ = false;
    bool focused_ = false;
    bool hovered_ = false;
    std::string tooltip_text_;
    // Set on this widget and propagated to every ancestor by Invalidate()/
    // RequestLayout(). Consumed by WidgetTree to decide whether a repaint or
    // relayout is actually needed.
    bool paint_dirty_ = false;
    bool layout_dirty_ = false;
    bool clips_children_ = false;
    Layout::Rect visible_rect_{};
    bool has_visible_rect_ = false;

    std::weak_ptr<Widget> parent_;
    std::vector<std::shared_ptr<Widget>> children_;
    // Children added before shared ownership existed (constructor-time
    // AddChild calls); promoted into children_ by FlushPendingChildren().
    std::vector<std::shared_ptr<Widget>> pending_children_;
    WidgetTree* tree_ = nullptr;
};

} // namespace Widgets
} // namespace XER::UI

#endif // XER_UI_WIDGETS_WIDGET_H
