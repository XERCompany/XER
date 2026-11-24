#ifndef XER_UI_WIDGETS_WIDGET_H
#define XER_UI_WIDGETS_WIDGET_H

#include <cstdint>
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
    // Letter keys: KeyDown chords are reported only while Ctrl is held
    // (editing/app shortcuts); KeyUp reports letters regardless so
    // held-key tracking (K+J/L combos) sees every release.
    A,
    B,
    C,
    D,
    E,
    F,
    G,
    H,
    I,
    J,
    K,
    L,
    M,
    N,
    O,
    P,
    Q,
    R,
    S,
    T,
    U,
    V,
    W,
    X,
    Y,
    Z,
    // Digit-row keys (Ctrl-held only, like the letters above).
    D0,
    D1,
    D2,
    D3,
    D4,
    D5,
    D6,
    D7,
    D8,
    D9,
    // Punctuation shortcuts (Ctrl-held only, like the letters above).
    Slash
};

// How keyboard focus arrived on a widget — the views
// FocusManager::FocusChangeReason split (kDirectFocusChange /
// kFocusTraversal / kFocusRestore collapsed to three reasons). Focus
// itself is tracked per widget either way; the reason only decides
// whether the focus ring paints: views-style, a mouse click must never
// show the ring — keyboard traversal and programmatic focus do.
enum class FocusReason {
    kPointer,       // mouse click / tap routed through WidgetTree
    kTraversal,     // Tab / Shift+Tab / AdvanceFocus
    kProgrammatic   // RequestFocus, direct SetFocused, overlay handoff
};

enum class TextDirection {
    Inherit,
    Ltr,
    Rtl
};

// ── Single-line text overflow ─────────────────────────────────────
// Renderer::DrawText wraps word-broken text inside the rect instead of
// clipping — a squeezed rect turns a label into a two-line mid-word
// break. There is no ellipsize option on TextStyle, so callers that
// must stay on one line fit the string first: returns `text` when it
// measures <= max_width, else the longest UTF-8-safe prefix plus "…"
// that does. Empty when even "…" doesn't fit.
std::string EllipsizeText(Rendering::Renderer& renderer,
                          std::string_view text,
                          const Rendering::TextStyle& style,
                          float max_width);

// ── Layout-time text measurement seam ────────────────────────────────
// GetMinSize/GetPreferredSize are const and run before the first
// Render — no Renderer is in scope at layout time. The host points
// this at the live renderer once one exists (Core::Window installs it
// automatically whenever its renderer changes); tests may install any
// Renderer with a stub MeasureText. With no target installed, widgets
// fall back to their documented estimates/measure caches — never a
// crash. Not owned: the host must call SetTextMeasureTarget(nullptr)
// before the renderer dies so no widget measures through a dangling
// pointer (Core::Window does this on teardown/recreation).
void SetTextMeasureTarget(const Rendering::Renderer* renderer);
const Rendering::Renderer* GetTextMeasureTarget();

enum class MouseButton {
    None,
    Left,
    Middle,
    Right
};

// ── Layout-geometry contract (Qt::Alignment flags model) ────────────
// How a widget positions itself inside a layout slot that is larger
// than its preferred size. Combine one horizontal flag with one
// vertical flag via operator| (e.g. Alignment::HCenter | Alignment::Top).
// Default keeps the legacy contract: the widget fills its whole slot.
enum class Alignment : std::uint32_t {
    Default  = 0x0000,
    // Horizontal axis — mutually exclusive.
    Leading  = 0x0001,   // left edge in LTR, right edge in RTL
    Left     = 0x0002,   // absolute — ignores text direction
    HCenter  = 0x0004,
    Right    = 0x0008,
    Trailing = 0x0010,   // right edge in LTR, left edge in RTL
    HFill    = 0x0020,   // stretch to the slot width
    // Vertical axis — mutually exclusive.
    Top      = 0x0100,
    VCenter  = 0x0200,
    Bottom   = 0x0400,
    VFill    = 0x0800,
    // Composites.
    Center   = 0x0004 | 0x0200,
    Fill     = 0x0020 | 0x0800
};

constexpr Alignment operator|(Alignment lhs, Alignment rhs) noexcept {
    return static_cast<Alignment>(
        static_cast<std::uint32_t>(lhs) | static_cast<std::uint32_t>(rhs));
}
constexpr Alignment operator&(Alignment lhs, Alignment rhs) noexcept {
    return static_cast<Alignment>(
        static_cast<std::uint32_t>(lhs) & static_cast<std::uint32_t>(rhs));
}
inline Alignment& operator|=(Alignment& lhs, Alignment rhs) noexcept {
    lhs = lhs | rhs;
    return lhs;
}
constexpr bool HasAlignmentFlag(Alignment value, Alignment flag) noexcept {
    return (value & flag) == flag;
}
// The horizontal/vertical flag bits in isolation (Default when unset).
constexpr Alignment HorizontalAlignment(Alignment a) noexcept {
    return a & static_cast<Alignment>(0x00FF);
}
constexpr Alignment VerticalAlignment(Alignment a) noexcept {
    return a & static_cast<Alignment>(0xFF00);
}

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
    // Same as SetFocused plus how focus arrived — the reason controls
    // focus-ring visibility (kPointer never paints the ring). The
    // one-arg overload is the programmatic path existing callers use.
    void SetFocused(bool focused, FocusReason reason);
    bool IsFocused() const;
    // True when the widget is focused AND the ring should paint —
    // keyboard traversal or programmatic focus, never a mouse click.
    // RenderFocusRing consults this; widgets that draw their own ring
    // should check it instead of IsFocused().
    bool IsFocusRingVisible() const;
    FocusReason GetFocusReason() const { return focus_reason_; }

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
    // SetBounds receives the *assigned layout slot*: the widget adopts
    // the rect produced by the box-model resolution (margin, indent,
    // fixed size, alignment — see below), which every Layout()
    // implementation funnels through automatically.
    Layout::Rect GetBounds() const;
    void SetBounds(const Layout::Rect& bounds);
    void Invalidate();
    // Repaint only `region` (same client coordinate space as GetBounds(),
    // clamped to the widget bounds) instead of the whole widget — for
    // widgets redrawing a small moving part (playhead line, caret).
    void Invalidate(const Layout::Rect& region);

    // ── Universal layout-geometry contract ──────────────────────────
    // Qt QWidget/QLayoutItem + Chromium views model: uniform properties
    // stored on Widget, honored centrally — SetBounds (the funnel every
    // Layout implementation and direct-placement host goes through)
    // resolves the assigned slot through the box model, so every
    // container honors margin/alignment/fixed size for every laid-out
    // child without per-container code. All defaults are no-ops —
    // existing widgets keep today's behavior.
    //
    // NOTE for container widgets: derive internal child rects from
    // GetBounds() after SetBounds, not from the raw `bounds` parameter,
    // so a non-default margin/alignment stays consistent inside the
    // widget too.

    // Alignment inside an oversized slot (see the Alignment flags at
    // the top of this header). Default fills the slot — legacy
    // behavior. An unset axis fills the slot unless a fixed extent
    // clamps it (Qt: a fixed-size widget packs at the cell start).
    void SetAlignment(Alignment alignment);
    Alignment alignment() const { return alignment_; }

    // Box-model margin: outer spacing carved out of the assigned slot
    // before the widget adopts its bounds. Honored by every parent
    // container automatically via SetBounds resolution.
    void SetMargin(Layout::Insets margin);
    Layout::Insets margin() const { return margin_; }

    // Extra leading-edge inset added on top of the margin during bounds
    // resolution — Qt tree-indent semantics (left edge in LTR, right
    // edge in RTL); also usable as a paragraph/hierarchy indent.
    // Negative values are clamped to 0.
    void SetIndent(int pixels);
    int indent() const { return indent_; }

    // Qt QLabel-style flag: when true, widgets with intrinsic content
    // (decoded video frames, images, custom-painted surfaces) stretch
    // the content to fill the adopted bounds ignoring aspect ratio.
    // Stored centrally — VideoPreviewWidget and Surface honor it
    // today; other widgets may consult it where meaningful.
    void SetScaledContents(bool scaled);
    bool hasScaledContents() const { return scaled_contents_; }

    // Qt naming aliases (concrete, non-virtual): sizeHint delegates to
    // GetPreferredSize(), minimumSizeHint() to GetMinSize().
    Layout::Size sizeHint() const { return GetPreferredSize(); }
    Layout::Size minimumSizeHint() const { return GetMinSize(); }

    // Qt-style geometry aliases over GetBounds(). Bounds are in parent
    // coordinates — the Qt geometry() equivalent, not Qt's local rect().
    Layout::Rect rect() const { return bounds_; }
    Layout::Point pos() const { return {bounds_.x, bounds_.y}; }
    Layout::Size size() const { return bounds_.GetSize(); }
    float x() const { return bounds_.x; }
    float y() const { return bounds_.y; }
    float width() const { return bounds_.width; }
    float height() const { return bounds_.height; }

    // Explicit geometry — thin aliases over SetBounds; the same
    // box-model resolution applies to the assigned rect.
    void Move(const Layout::Rect& rect);
    void Move(int x, int y);               // reposition, keep the size
    void Resize(int w, int h);             // resize, keep the position
    void Resize(const Layout::Size& size);

    // Qt fixed-size policy: a fixed extent overrides the preferred size
    // on that axis during bounds resolution — Fill/alignment cannot
    // stretch it. Pass <= 0 (or ClearFixedSize) to unset an axis.
    void SetFixedSize(const Layout::Size& size);
    void SetFixedSize(float w, float h);
    void SetFixedWidth(float w);
    void SetFixedHeight(float h);
    void ClearFixedSize();
    bool HasFixedWidth() const { return fixed_width_ >= 0.0f; }
    bool HasFixedHeight() const { return fixed_height_ >= 0.0f; }
    float FixedWidth() const { return fixed_width_; }
    float FixedHeight() const { return fixed_height_; }

protected:
    Widget() = default;

    void RequestLayout();

    // Draws the shadcn-style outer focus ring around the widget bounds
    // when the ring should paint — i.e. IsFocusRingVisible() (focused via
    // keyboard traversal or programmatic request, never a mouse click).
    // `radius` is the widget's own corner radius.
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

    // Applies the box-model contract to `cell` (the assigned layout
    // slot): subtracts margin + indent, clamps fixed extents, positions
    // the preferred size per alignment(). Identity when all properties
    // are default — keeps the legacy adopt-the-slot behavior.
    Layout::Rect ResolveBoundsInCell(const Layout::Rect& cell) const;

    void ClearSubtreeInteractionState();
    void SetTreeRecursive(WidgetTree* tree);
    // Registers children that were AddChild()ed while the widget was still
    // under construction (shared ownership not yet established, so
    // weak_from_this() was empty). Called from SetTreeRecursive, i.e. as soon
    // as the widget joins a WidgetTree.
    void FlushPendingChildren();

    Layout::Rect bounds_;
    // Box-model state for the universal layout-geometry contract.
    Layout::Insets margin_{};
    Alignment alignment_ = Alignment::Default;
    int indent_ = 0;
    bool scaled_contents_ = false;
    float fixed_width_ = -1.0f;   // < 0 = unset
    float fixed_height_ = -1.0f;  // < 0 = unset
    Theme::Tokens cached_tokens_{};
    bool visible_ = true;
    bool enabled_ = true;
    bool focusable_ = false;
    bool focused_ = false;
    // Cleared widgets reset to kProgrammatic so a stale kPointer can't
    // suppress the ring on the next traversal/programmatic focus.
    FocusReason focus_reason_ = FocusReason::kProgrammatic;
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
