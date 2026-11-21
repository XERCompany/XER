#ifndef XER_UI_WIDGETS_WIDGET_H
#define XER_UI_WIDGETS_WIDGET_H

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "layout/geometry.h"
#include "theme/tokens.h"

namespace XER::Content::CSS {
class ComputedStyle;
}

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
    F5,
    // Letter keys are reported only while Ctrl is held (editing/app shortcuts).
    A,
    C,
    D,
    F,
    K,
    N,
    O,
    P,
    S,
    V,
    X,
    Y,
    Z,
    // Punctuation shortcuts (Ctrl-held only, like the letters above).
    Slash
};

enum class TextDirection {
    Inherit,
    Ltr,
    Rtl
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

    // Pressed/active state (:active CSS pseudo-class). Interactive
    // widgets set this while the primary button is held on them.
    void SetActive(bool active);
    bool IsActive() const { return active_; }

    // ── CSS style identity (XER::Content::CSS bridge) ───────────
    // Tag name for element selectors; the default "" lets the style
    // host derive a name from RTTI (e.g. "button" for Button).
    virtual std::string_view GetStyleTag() const { return {}; }
    // Element id for #id selectors.
    void SetStyleId(std::string id);
    std::string_view StyleId() const { return style_id_; }
    // Class list for .class selectors.
    void AddStyleClass(std::string cls);
    void RemoveStyleClass(std::string_view cls);
    bool HasStyleClass(std::string_view cls) const;
    const std::vector<std::string>& StyleClasses() const {
        return style_classes_;
    }
    // Attributes for [attr] / [attr=value] selectors. "disabled" is
    // mirrored automatically from IsEnabled().
    void SetStyleAttribute(std::string name, std::string value);
    const std::vector<std::pair<std::string, std::string>>&
    StyleAttributes() const {
        return style_attributes_;
    }
    // Inline declarations (style="..." equivalent): applied after all
    // matched rules in the cascade.
    void SetInlineStyle(std::string property, std::string value);
    const std::vector<std::pair<std::string, std::string>>&
    InlineStyle() const {
        return inline_style_;
    }
    // Resolved style, written by CSS::WidgetStyleHost::Update(). Widgets
    // may consult it in Render/Layout for CSS-driven visuals.
    void SetComputedStyle(
        std::shared_ptr<const Content::CSS::ComputedStyle> style);
    std::shared_ptr<const Content::CSS::ComputedStyle>
    GetComputedStyle() const {
        return computed_style_;
    }
    // Typed read of one computed property (nullptr when unset).
    const std::string* StyleValue(std::string_view property) const;

    // shadcn-style tooltip: text shown in a floating bubble after a short
    // hover delay (timing + positioning managed by WidgetTree). Empty
    // string disables the tooltip for this widget.
    void SetTooltip(std::string text);
    const std::string& GetTooltip() const { return tooltip_text_; }

    // DevTools/inspector hook: short human-readable content of the widget
    // (label text, button caption, input value...). Empty by default;
    // DebugMapUI emits it as the `text` attribute when non-empty.
    virtual std::string GetDebugText() const { return {}; }

    // Text direction for this widget and (through EffectiveTextDirection)
    // its descendants. Inherit resolves to the nearest ancestor's
    // direction, defaulting to Ltr at the root.
    void SetTextDirection(TextDirection direction);
    TextDirection GetTextDirection() const { return text_direction_; }
    TextDirection EffectiveTextDirection() const;

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

    // Bounds in parent coordinates and repaint request — public so hosts
    // (Node addon, tests) can position widgets without a layout pass.
    Layout::Rect GetBounds() const;
    void SetBounds(const Layout::Rect& bounds);
    void Invalidate();

protected:
    Widget() = default;

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
    bool active_ = false;
    std::string tooltip_text_;
    // CSS identity/state consumed by CSS::WidgetStyleHost.
    std::string style_id_;
    std::vector<std::string> style_classes_;
    std::vector<std::pair<std::string, std::string>> style_attributes_;
    std::vector<std::pair<std::string, std::string>> inline_style_;
    std::shared_ptr<const Content::CSS::ComputedStyle> computed_style_;
    // Set on this widget and propagated to every ancestor by Invalidate()/
    // RequestLayout(). Consumed by WidgetTree to decide whether a repaint or
    // relayout is actually needed.
    bool paint_dirty_ = false;
    bool layout_dirty_ = false;
    bool clips_children_ = false;
    Layout::Rect visible_rect_{};
    bool has_visible_rect_ = false;
    TextDirection text_direction_ = TextDirection::Inherit;

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
