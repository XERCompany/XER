# UI Widget API — universal layout-geometry contract

Base-API wave landed 2026-11-22 on `src/ui`. Every `Widget` in
`src/ui/widgets/` (~70) now carries the layout-geometry properties
apps expect from Qt's `QWidget`/`QLayoutItem` and Chromium's
`ui/views`. Sources: `widgets/widget.h`, `widgets/widget.cc`,
`layout/flex.{h,cc}`, `layout/geometry.h`, `widgets/surface.{h,cc}`.

## Where honoring happens

`Widget::SetBounds` is the single funnel every `Layout()`
implementation and every direct-placement host
(`Resizable` panels, `Carousel` slides, `WidgetTree` root/overlay,
`xml_ui.cc` host placement) passes through. The box-model resolution
lives in `Widget::ResolveBoundsInCell(cell)`:

1. subtract `margin()` + `indent()` (indent on the direction-aware
   leading edge: left in LTR, right in RTL),
2. resolve per-axis extent — fixed size wins over preferred, an unset
   alignment axis fills the remaining box, a fixed extent under Fill
   packs at the cell start (Qt behavior for fixed-size widgets),
3. position the resolved size inside the box per `alignment()`
   (`Layout::AlignedRect` does the geometry).

All defaults are identity: margin `{0,0,0,0}`, `Alignment::Default`,
`indent() == 0`, fixed extents unset → `GetBounds()` returns the
assigned slot verbatim, so nothing regresses.

**Container note:** composite widgets that lay out children from the
raw `bounds` parameter keep working, but for a non-default
margin/alignment on the composite itself, internal rects should be
derived from `GetBounds()` after `SetBounds` — the adopted rect.
Tools doing manual placement can share the same math via
`Layout::AlignedRect` / `Layout::AlignMode` (`layout/flex.h`).

## API shipped on `Widget`

```cpp
// Qt::Alignment-style flags — combine one horizontal + one vertical
// bit: Alignment::HCenter | Alignment::Top.
enum class Alignment : std::uint32_t {
    Default  = 0x0000,                 // fill the slot (legacy)
    Leading  = 0x0001,  // left in LTR, right in RTL
    Left     = 0x0002,  // absolute, direction-independent
    HCenter  = 0x0004,
    Right    = 0x0008,
    Trailing = 0x0010,  // right in LTR, left in RTL
    HFill    = 0x0020,
    Top      = 0x0100,
    VCenter  = 0x0200,
    Bottom   = 0x0400,
    VFill    = 0x0800,
    Center   = HCenter | VCenter,
    Fill     = HFill | VFill
};
// constexpr operator|, operator&, operator|=; HasAlignmentFlag(),
// HorizontalAlignment(), VerticalAlignment() helpers.

void SetAlignment(Alignment);         Alignment alignment() const;
void SetMargin(Layout::Insets);       Layout::Insets margin() const;
void SetIndent(int pixels);           int indent() const;       // >= 0
void SetScaledContents(bool);         bool hasScaledContents() const;

Layout::Size sizeHint() const;         // → GetPreferredSize()
Layout::Size minimumSizeHint() const;  // → GetMinSize()

Layout::Rect rect() const;   Layout::Point pos() const;
Layout::Size size() const;
float x() const;  float y() const;
float width() const;  float height() const;

void Move(const Layout::Rect&);   // assigned slot, same resolution
void Move(int x, int y);          // reposition, keep size
void Resize(int w, int h);        // resize, keep position
void Resize(const Layout::Size&);

void SetFixedSize(const Layout::Size&);   // axis <= 0 leaves it free
void SetFixedSize(float w, float h);
void SetFixedWidth(float);   void SetFixedHeight(float);
void ClearFixedSize();
bool HasFixedWidth() const;    float FixedWidth() const;   // <0 unset
bool HasFixedHeight() const;   float FixedHeight() const;
```

`Layout::Insets` is a new alias of `Layout::EdgeInsets` in
`geometry.h` (same POD — either spelling works), plus
`EdgeInsets::IsEmpty()/Horizontal()/Vertical()` helpers.

`Layout::AlignMode` (`Fill/Leading/Center/Trailing`) +
`Layout::AlignedRect(cell, size, h, v)` in `layout/flex.h` are the
widget-free geometry primitives the resolution uses — available to
tools that place children by hand (`DistributeRow` users etc.).

## Qt / views mapping

| XER `Widget` | Qt | views |
|---|---|---|
| `alignment()` / `SetAlignment` | `QLayoutItem::alignment` (per-axis `Qt::Alignment` flags) | `LayoutManager` child alignment |
| `margin()` / `SetMargin` | `QMargins` on the layout item | `View::SetBorder`/property insets |
| `indent()` / `SetIndent` | `QTreeView::indentation` (leading-edge inset) | — |
| `hasScaledContents()` | `QLabel::scaledContents` | — |
| `sizeHint()` / `minimumSizeHint()` | `QWidget::sizeHint` / `minimumSizeHint` | `View::GetPreferredSize` / `CalculatePreferredSize` |
| `rect()`/`pos()`/`size()`/`x()`/`y()`/`width()`/`height()` | `QWidget::geometry()` accessors (parent coords!) | `View::bounds()`/`x()`/`y()`/`width()`/`height()` |
| `Move` / `Resize` | `QWidget::move` / `resize` (via SetBounds) | `View::SetBoundsRect` |
| `SetFixedSize/Width/Height` | `QSizePolicy::Fixed` + `setFixedSize` | `View::SetPreferredSize` |
| `GetBounds`/`SetBounds` (existing) | `QWidget::setGeometry` — assigns the *slot* | `View::SetBoundsRect` |
| `GetMinSize`/`GetPreferredSize`/`GetMaxSize` (existing) | `minimumSize`/`sizeHint`/`maximumSize` | `GetMinimumSize`/`GetPreferredSize` |
| `Surface` (new) | `QOpenGLWidget`-style paint host | `views::View` + `OnPaint` override |

## Semantics notes

- **Alignment** applies only when the slot is larger than preferred on
  that axis — tighter slots shrink the widget into the box (Fill).
- **Fixed size** overrides preferred per axis; under `Fill`/unset
  alignment the extent is fixed and packed leading (cell start) —
  matches Qt fixed-size widgets in a stretch cell.
- **Indent** is additive leading-edge spacing (tree/paragraph indent),
  resolved against `EffectiveTextDirection()` — RTL mirrors it to the
  right edge.
- **Margin** is box-model *outer* spacing: the assigned slot shrinks
  before the widget adopts bounds. Containers never see the margin.
- **scaledContents** is stored centrally and honored by widgets with
  intrinsic content: `VideoPreviewWidget` (skips letterboxing) and
  `Surface` (ContentRect stretch vs letterbox). Other widgets may
  consult `hasScaledContents()` where meaningful.
- `Move`/`Resize`/`SetBounds` all take the *assigned slot*; the widget
  adopts `ResolveBoundsInCell(slot)`. `rect()`/`x()`/`y()`/`w`/`h`
  report the adopted bounds in parent coordinates (Qt `geometry()`,
  not Qt's local `rect()`).

## `Surface` — the custom-paint primitive

`widgets/surface.h`: a `Widget` that forwards `Render` to an
app-installed paint callback — the primitive the VE monitor/timeline
rebuild needed (HWND `Canvas` stays reserved for plug-views).

```cpp
using PaintCallback =
    std::function<void(Rendering::Renderer&, const Layout::Rect& content,
                       const Theme::Tokens&)>;
using EventCallback = std::function<bool(const Event&)>;
using SizeCallback  = std::function<Layout::Size()>;

void SetPaintCallback(PaintCallback);    // nullptr disables painting
void SetEventCallback(EventCallback);    // auto-SetFocusable(true)
void SetPreferredSizeCallback(SizeCallback);  // dynamic pref size
void SetMinSize/SetPreferredSize/SetMaxSize(Layout::Size);
void SetContentSize(Layout::Size);       // intrinsic size → letterbox
Layout::Rect ContentRect() const;        // honors scaledContents
```

Paint contract: `Render` pushes `PushClip(GetBounds())` then invokes
the callback with `ContentRect()`; input flows through `HandleEvent`
to the event callback. `ContentRect` letterboxes `ContentSize` inside
the bounds preserving aspect unless `hasScaledContents()` is set
(stretch) or content size is `{0,0}` (full bounds). Partial repaint
via `Invalidate(rect)`.

## Files

- `src/ui/widgets/widget.h` — Alignment flags + property declarations
- `src/ui/widgets/widget.cc` — `ResolveBoundsInCell`, all setters
- `src/ui/layout/geometry.h` — `Insets` alias + EdgeInsets helpers
- `src/ui/layout/flex.h/.cc` — `AlignMode` + `AlignedRect`
- `src/ui/widgets/surface.h/.cc` — custom-paint widget
- `src/ui/widgets/video_preview_widget.cc` — honors scaledContents

Verified: `video_editor --smoke` on `windows-clang-msys2`. Two
check failures (`builtin_fx` table size, `proxy_workflow` LRU sweep)
are in subsystems with no `src/ui` code path — unrelated to this wave.
