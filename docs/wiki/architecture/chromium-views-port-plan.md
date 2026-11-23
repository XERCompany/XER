# Chromium ui/views → src/ui port plan

Research + port plan dated 2026-11-23. Local sources read:
`src/ui/ARCHITECTURE.md`, `widgets/widget.{h,cc}`,
`widgets/widget_tree.{h,cc}`, `layout/{geometry,flex}.{h,cc}`,
`theme/tokens.h`, `canvas/canvas.h`, `core/{application,window,
render_host,ui_events}.{h,cc}`, `motion/motion.h`, `widgets/label.*`,
`widgets/button.h`, `widgets/text_input.h`, `widgets/tooltip.h`,
`rendering/renderer.h`, plus ui2's `shortcut_router.h` and
`drag_session.h` (app-side, not kit). Chromium sources read on
chromium.googlesource / GitHub mirror `chromium/chromium@main`:
`ui/views/view.h`, `ui/views/layout/{layout_manager,flex_layout,
flex_layout_types}.h`, `ui/views/widget/widget.h`,
`ui/views/focus/focus_manager.h`, `ui/views/controls/label.h`,
`ui/views/animation/{animation_builder,ink_drop}.h`,
`ui/events/{event_handler,event_target}.h`. Cross-ref:
[ui-widget-api.md](ui-widget-api.md) (the Qt/views mapping for the
layout-geometry contract already landed 2026-11-22).

## 0. The one-paragraph model difference

views is **retained mode**: every `View` can own a `ui::Layer`, paint
is recorded into the layer by the compositor on a vsync schedule, and
"repaint" means the compositor re-issues textures. XER src/ui is
**immediate mode with explicit damage**: `Widget::Render(renderer,
tokens)` re-issues draw calls every frame into a retained D2D canvas
(or a fully immediate Vulkan/software path), and
`Widget::Invalidate`/`AddDamageRect` keeps a scattered rect list that
drives per-rect `InvalidateRect` + per-widget culling
(`widget_tree.cc` `RenderSubtree`). We also push the whole frame
through `StreamRenderer` to remote `xer_engine_ui_render.exe` workers
(RenderStore shared memory) — views has no equivalent.

**Everything in this plan filters through that difference.** The
views *contract surface* (View API, layout rules, focus model, event
order, accelerator priority) ports cleanly. Anything whose
implementation assumes a per-View GPU layer — ink-drop ripples on
their own layer, `LayerAnimator` transform animations, compositor
clip paths — must be re-expressed as value animators (`Motion::Spring`
/`Tween`) driving widget properties + `Renderer::SetTransform`/
`PushLayer` per frame.

## 1. Side-by-side mapping

### Core object model

| views | src/ui | Verdict |
|---|---|---|
| `views::View` | `Widgets::Widget` | Present. shared_ptr tree, weak parent chain, bounds in parent coords, min/pref/max size, visibility/enabled/focus flags, tooltip text, CSS identity. ~83 widget subclasses. |
| `internal::RootView` | `Widgets::WidgetTree` | Present, wider: root + modal overlay + passive overlay (tooltips) + event router + focus owner + damage tracker + style host. |
| `views::Widget` + `internal::NativeWidget` (HWNDNativeWidget) | `Core::Window` + `platform/windows/input_translator` | Merged into one class — fine on a Windows-only codebase. Missing the InitParams::Type taxonomy (POPUP/MENU/TOOLTIP/BUBBLE/DRAG top-levels); see T11. |
| `DesktopWindowTreeHost` (aura) | `Window::Impl` WndProc + `input_translator.cc` | Present, Win32-only. No touch/WM_POINTER → no gesture layer possible today. |
| `ui::Compositor` + `ui::Layer` | none — `Rendering::Renderer` immediate mode + `Canvas` child-HWND Skia surfaces | Different model (keep ours). `Renderer::PushLayer/PopLayer` approximates per-element opacity/blend/blur compositing for one frame. |
| `ui::LayerAnimator` / `views::AnimationBuilder` | `Motion::Spring`/`Tween` + `Widget::Tick(dt)` driven by `Window::SetFrameCallback` → `TickTree` | Value-level animation only; no scheduler, no property-timeline API, no abort handles. No "TokenAnimator" exists — that name in the task = Tween. |
| `gfx::Canvas` / `PaintContext` | `Rendering::Renderer` | Our interface. Smaller op set (FillRect/StrokeRect/DrawText/DrawImage/DrawFrameBgra/clips/layers/2×3 transform). `MeasureText` exists but is renderer-reachable only at paint time — affects T1. |
| `views::FocusManager` | inlined in `WidgetTree` (`CollectFocusable`, `MoveFocus`, `SetFocusedWidget`, Tab/Shift+Tab) | Partial: wraps, DFS order, overlay scope ✓. Missing: focus-change **reason** (mouse click shows a ring — views only shows it for `kFocusTraversal`/accessibility), Store/RestoreFocusedView on activation loss, focus listeners, `FocusTraversable` for nested trees (our child-HWND plug views!), Focus/Blur events per widget (ours only fire on window WM_SETFOCUS/KILLFOCUS). |
| `views::TooltipManager` | `WidgetTree` tooltip fields + `Tooltip` passive overlay | Partial: 600 ms hover delay, tracks anchor, input-transparent ✓. Missing: per-point anchor override, keyboard-triggered tooltips, tooltip regions smaller than bounds. |
| `DragController` + `OSExchangeData` + drop-target protocol | none in kit; `ui2::BinDragSession` is app-local shell state | Missing. No `CanStartDrag`/`OnDrop` protocol on Widget, no OS OLE bridge — drags can't leave or enter the HWND. |
| `ui::AcceleratorManager` + `Accelerator` | `ui2::ShortcutRouter` installed via `WidgetTree::SetKeyHandler` | Present but app-side; each app re-implements. Also the kit's `KeyCode` enum only enumerates {A,C,D,F,K,M,N,O,P,S,V,X,Y,Z,Slash} — **Ctrl+B/E/G/H/I/J/L/Q/R/T/U/W cannot be bound at all**. |
| `EventTargeter`/`ViewTargeter` | `WidgetTree::FindHitTarget` (bounds.Contains, reverse children order, `clips_children_` gate) | Partial: no hit-test mask, no pluggable targeter, ignores paint-time CSS transforms. |
| `EventTarget` pre/post-target handlers (priorities kAccessibility/kSystem/kDefault) | `UiEventBus` filters — window scope only | Partial: bus covers the whole tree but a widget can't snoop/steer its subtree's events; context menus are a single tree-level `context_menu_handler_` fallback instead. |
| `GestureRecognizer`/`GestureProvider` | none | Missing — no touch input path. Defer (mouse/keyboard suite). |
| `ViewAccessibility`/`AXNodeData`/`AXMode` | none | Missing entirely — no UIA/MSAA. |
| `ui::InputMethod` (IME) | none — WM_CHAR only | Missing: no CJK/IME composition into TextInput. |

### LayoutManager ↔ our layout

| views | src/ui | Verdict |
|---|---|---|
| `LayoutManager` interface (`Layout`, `GetPreferredSize`, `GetMinimumSize`, `GetPreferredHeightForWidth`, `InvalidateLayout`, `ViewAdded/Removed`, `SetViewVisibility`) | none — every container hand-rolls `Layout(bounds)`; helpers live as free functions (`layout/flex.{h,cc}`: `AlignedRect`, `CenteredRect`, `DistributeRow`, `Snap*`) | Missing the abstraction. A `Layout()` override IS the layout manager — nothing declarative to compose. |
| `FlexLayout` DSL (builder chain, `kFlexBehaviorKey`, `kCrossAxisAlignmentKey`, `kMarginsKey`, `kInternalPaddingKey`, `kViewIgnoredByLayoutKey`) | `Widgets::Widget` margin/alignment/indent/fixed-size contract (resolved in `SetBounds`) + `DistributeRow` grow_weights | Partial parity on the *property* side (ours is arguably simpler — every container honors it for free), but no per-child flex rule/weight/order, no drop-out semantics, row-only helper (no `DistributeColumn`). |
| `FlexSpecification` (`MinimumFlexSizeRule` × `MaximumFlexSizeRule` × weight × order) | `DistributeRow` proportional shrink to `max(8, base*0.28)` floor + grow weights | Missing: no snap-to-zero/drop-out (the toolbar-overflow pattern), no allocation `order`, no `kScaleToMinimum` honest floor (ours invents 28% of base). |
| `GetPreferredHeightForWidth` | `Widget::GetContentHeight` (scroll contents only) | Missing: no width-dependent measure → no true multiline Label. |

### Controls ↔ our widgets

| views | src/ui | Verdict |
|---|---|---|
| `views::Label` (RenderText: `SetElideMode`, `SetMaxLines`, `SetObscured`, selectable, subpixel control, auto-color-readability, `IsDisplayTextClipped`) | `Widgets::Label` | Much weaker: `GetMinSize() {0,0}` lies, `GetPreferredSize` = 7.7 px/char **heuristic** (never measured!), `GetMaxSize` = 1e5, no elide/wrap/obscure. Overflow just clips. |
| `views::Textfield` (RenderTextHarfBuzz, selection model, cursor blink, IME, DnD, context menu) | `Widgets::TextInput` (caret, anchor selection, undo/redo, password mask, scroll_x, clipboard) | Solid core; missing IME, shaped text (no complex scripts), drag-select-to-scroll edge cases, DnD of text. |
| `LabelButton`/`MdTextButton` + `InkDrop` + `FocusRing` | `Widgets::Button` (5 styles × 3 sizes, icon, checkable, default, menu, loading, `Motion::Spring press_`, hover tween) | Functional parity; visual feedback model simpler (spring vs InkDropState machine). `RenderFocusRing` used by 19 widgets. |
| `MenuButton`, `MenuModelAdapter`, `MenuRunner` | `ContextMenu` overlay widget + `Button::SetMenu` | Present in-tree; can't exceed window bounds (overlay clipped to client area) — a real top-level HWND menu needs T11. |

## 2. What views does better — adopt list (concrete)

1. **Focus-change reason + keyboard-only focus ring.** `FocusManager`
   tracks `FocusChangeReason{kFocusTraversal, kDirectFocusChange,
   kFocusRestore, kFocusNativeView}`; the focus ring only paints when
   focus arrived by keyboard. **We draw `RenderFocusRing` on
   mouse-clicked widgets** — `MouseDown` → `SetFocusedWidget` → ring
   (`widget_tree.cc:431-433`). This is the single cheapest real UX
   fix in the list.
2. **Store/restore focus across window activation.** views stashes
   the focused view on `WM_KILLFOCUS` and restores on reactivation;
   ours leaves `focused_` set but never delivers per-widget
   Focus/Blur events on *tree* focus changes (Focus/Blur only fire
   on window activation today).
3. **Label honesty + elision.** gfx::RenderText tail/middle elision,
   `SetMaxLines`, min-size accounting for elision. Our
   `GetPreferredSize` never calls `MeasureText` — it's a 7.7 px/char
   guess.
4. **FlexSpecification drop-out rules.** `MinimumFlexSizeRule`
   (kScaleToZero / …SnapToZero / kScaleToMinimum / kPreferred…) +
   `order` priority allocation lets a toolbar *drop views that don't
   fit* — exactly the NLE transport-bar/inspector-resize pattern.
   `DistributeRow` can't express "remove me".
5. **Per-widget class properties for layout** (`kMarginsKey`,
   `kCrossAxisAlignmentKey`, `kInternalPaddingKey`,
   `kViewIgnoredByLayoutKey`). We already have margin/alignment on
   Widget — extend with `kIgnoredByLayout` + per-child cross-axis
   override rather than cloning ClassProperty machinery.
6. **`GetPreferredHeightForWidth`** — width-dependent measure. Needed
   for any multiline Label/TextArea-in-flex.
7. **Full `gfx::Transform` per view, honored by hit-testing.** We
   parse only `translate()`/`scale()` from CSS `transform` at paint
   time (`widget_tree.cc` `ParseStyleTransform`); no `rotate()`, and
   `FindHitTarget` uses raw `bounds_` so a transformed subtree gets
   *untransformed* input — inconsistent.
8. **Pre/post-target EventHandler lists with priorities**
   (kAccessibility → kSystem → kDefault). Ours has only the
   window-scope `UiEventBus` filter chain. Per-widget interception
   would retire the ad-hoc `context_menu_handler_`/`key_handler_`
   fallbacks.
9. **Accelerator registry with priorities on the Widget/kit level** —
   plus the `KeyCode` enum must become complete (raw VK or all
   letters/digits) before Ctrl+E/R/B… can even be expressed.
10. **Drag-and-drop protocol + manager.** views: drag source
    (`WriteDragData`/`GetDragOperations`), drop target
    (`CanDrop`/`OnDragEntered`/`OnDragOver`/`OnDragExit`/`OnDrop`),
    `OSExchangeData` bridging OLE so drags cross window/process
    boundaries. Ours: nothing in the kit; ui2 hand-rolls
    `BinDragSession` per app.
11. **Tooltip anchoring per point** (`GetTooltipHandlerForPoint`):
    tooltips for *regions* inside a widget (timeline clip vs gap),
    keyboard-focus tooltips, configurable delay.
12. **Accessibility tree** (`ViewAccessibility` → AXNodeData
    name/role/state). Zero today — no UIA/MSAA/AT-SPI. Defer but
    design the Widget API (name/role/state getters) so a future
    bridge is additive.
13. **Widget InitParams type taxonomy** — real top-level HWNDs for
    menus/tooltips/bubbles so chrome can extend past the client
    rect; overlays can't escape the window today.
14. **IME** (ui::InputMethod → composition events). Needed for a
    professional suite eventually; pure gap today.
15. **Z-order API** — views has `ReorderChildView`/z-index; ours has
    children order only (no reorder call).

### Already at parity — do NOT re-port

- Paint scheduling: `SchedulePaint`/`SchedulePaintInRect` ↔
  `Invalidate`/`Invalidate(region)` + `AddDamageRect` +
  `MergeDamageRect` cap. Ours is arguably better (scattered-rect
  damage vs bounding-box update regions).
- Layout invalidation: `InvalidateLayout` ↔ `RequestLayout` +
  `layout_dirty_` propagated to ancestors + `ConsumeLayoutRequest`
  in `Window::FlushDirtyState`. **We do not re-layout every
  frame** — layout runs on resize/RequestLayout only.
- Bubbling: views dispatches up the target path; our
  `DispatchClick`/MouseWheel walk `GetParent()` — same semantics.
- Mouse capture during drags (`left_mouse_down_target_` + synthetic
  MouseUp via `CancelPointerCapture`) ≈ views' capture client.
- Modal layering (`overlay_` owns input, Escape reach, focus move
  into overlay) ≈ views modal + RootView.
- Hover enter/exit + tooltip delay ≈ TooltipManager basics.
- DPI-snapped geometry (`Snap`/`SnapRect`) ≈ DIP layer.

## 3. What we do better / differently — keep

- **Immediate-mode + explicit damage.** For a 60 fps NLE canvas
  (playhead, meters, waveforms) re-issuing draw calls with per-rect
  culling is simpler and cheaper than per-View GPU layers; the
  `FrameWidgetStats`/PerfHud counters already prove it observable.
- **Remote render workers** (`UiRenderHost` + `StreamRenderer` +
  `RenderStore` shared memory): process-separated painting with
  pixel resources (video frames, spectra) resolved worker-side. views
  has nothing comparable.
- **`Theme::Tokens` semantic palette** (28 colors + metrics, JSON
  theme loader, light/dark variants) — flatter than views'
  ColorProvider pipeline and shadcn-compatible.
- **CSS bridge** (`WidgetStyleHost`: tag/id/class/attr selectors,
  inline style, per-element opacity/filter/transform compositing) —
  views' class-property system is weaker for declarative styling.
- **Universal box-model contract on `SetBounds`** — margin,
  alignment, indent, fixed size honored by *every* container
  through one funnel, zero per-container code (verified
  ui-widget-api.md).
- **HWND `Canvas` plug surfaces** for native video/plugin windows —
  simpler interop than aura `gfx::NativeView` embedding.
- Single `Window` owning HWND+tree+theme+eventbus — the
  Widget/NativeWidget/DesktopWindowTreeHost triple split buys
  cross-platform plumbing we don't need (Windows-only).

## 4. Concrete port tickets (ranked)

Effort: **S** ≤1 day, **M** 1–3 days, **L** >3 days.

| # | Ticket | Files | Effort |
|---|--------|-------|--------|
| T1 | Label honesty + elision | `widgets/label.{h,cc}`, `rendering/renderer.h` (+measure-provider seam) | **M** |
| T2 | Keyboard-only focus ring + focus reasons | `widgets/widget_tree.{h,cc}`, `widgets/widget.{h,cc}`, `core/ui_events.h` | **M** |
| T3 | Complete KeyCode / kit accelerator registry | `widgets/widget.h` (enum), `platform/windows/input_translator.cc`, new `core/accelerator_router.{h,cc}` (port of ui2 `shortcut_router.*`) | **M** |
| T4 | FlexLayout manager + FlexSpecification | new `layout/flex_layout.{h,cc}` (+ `layout/layout_manager.h`), container widget or `widgets/card.cc`-style host | **M** |
| T5 | Global DragDropManager (kit-internal) | new `widgets/drag_drop.{h,cc}`, `widgets/widget.{h,cc}` (virtuals), `widget_tree.cc` (session routing) | **L** |
| T6 | Widget transforms incl. hit-test | `widgets/widget.{h,cc}` (`SetTransform`), `widget_tree.cc` (`FindHitTarget` inverse-transform, damage map), style transform parse → `rotate()` | **L** |
| T7 | AnimationBuilder on widgets | new `motion/animator.{h,cc}` (+ `motion/motion.h` reuse) | **M** |
| T8 | Per-widget pre/post-target handlers | `widgets/widget.{h,cc}`, `widget_tree.cc` dispatch | **M** |
| T9 | Tooltip manager upgrade | `widgets/widget.h` (`GetTooltipRectForPoint`), `widget_tree.cc`, `widgets/tooltip.*` | **S** |
| T10 | Per-widget context-menu controller | `widgets/widget.h` (`virtual bool ShowContextMenuAt(Point)`), retire `context_menu_handler_` fallback | **S** |
| T11 | Popup widget type (real top-level HWND) | `core/window.{h,cc}`, `widget_tree.cc` overlay→window promotion for menus/tooltips | **L** |
| T12 | `GetHeightForWidth` + multiline Label | `widgets/widget.h`, `layout/*`, label | **M** |
| T13 | InkDrop-style interaction state machine | `widgets/widget.{h,cc}` (`InteractionState` enum), control widgets | **M** |
| T14 | Accessibility skeleton (UIA) | `widgets/widget.h` (name/role/state getters), new `core/accessibility/*`, window IAccessible bridge | **L** |
| T15 | IME composition events | `platform/windows/input_translator.cc` (WM_IME_*), `widgets/text_input.cc` | **L** |
| T16 | `ReorderChild` z-order API | `widgets/widget.{h,cc}` | **S** |

### Sketches

**T1 — Label.** Add `enum class ElideMode {kNone,kTail,kMiddle}`
(+`SetElideMode`), real `MeasureText`-backed sizes. Catch:
`GetMinSize/GetPreferredSize` are const with no renderer — introduce a
`TextMeasurer` seam (either a `static TextMeasureProvider` the
`Renderer`/host installs at `CreateRenderer`, mirroring
`Button::SetIconResolver`'s static-resolver pattern, or a measure
cache the first `Render` populates). `GetMinSize` → elided "A…"
width; `GetPreferredSize` → measured; `IsDisplayTextClipped()` →
tooltip escalation hook.

**T2 — Focus reason.** `enum class FocusChangeReason
{kTraversal,kDirect,kRestore,kNativeView}` on `SetFocusedWidget(widget,
reason)`; `Widget::SetFocused(focused, reason)`; `RenderFocusRing`
only when `reason==kTraversal` (or an `IsFocusRingVisible` query —
views splits `focused_` vs focus-ring visibility). Deliver synthetic
`Event::Type::Focus`/`Blur` to the losing/gaining widget inside
`SetFocusedWidget` (they currently only fire on window activation).
Add `FocusChangeListener` subscription through `UiEventBus` for
inspector/DevTools.

**T3 — Accelerators.** `KeyCode` → carry raw Win32 VK (or add all
letters/digits/F-keys + punctuation); `input_translator` reports all
Ctrl+letter chords, not the curated subset. Move
`tools/video_editor/ui2/shortcut_router.{h,cc}` into
`src/ui/core/accelerator_router.*` unchanged shape (scope + label +
decline-fallback semantics already proved out in ui2 smoke), install
as default `key_handler_`.

**T4 — FlexLayout.** New `Layout::FlexLayout` object a container
installs (or a `FlexBox` widget owning it). API:

```cpp
layout.SetOrientation(Orientation::kVertical)
      .SetMainAxisAlignment(MainAxis::kStart)
      .SetCrossAxisAlignment(CrossAxis::kStretch)
      .SetInteriorMargin(Insets{8}).SetCollapseMargins(true)
      .SetMinimumCrossAxisSize(24.f)
      .SetDefaultFlexRule(FlexSpecification(
           MinimumFlexSizeRule::kScaleToMinimumSnapToZero,
           MaximumFlexSizeRule::kUnbounded).WithWeight(1).WithOrder(1));
child->SetFlexSpecification(...);           // per-child override
child->SetProperty(kIgnoredByLayout,true);  // or SetIgnoredByLayout
```

Copy views' enum sets verbatim (they're well-designed:
`MinimumFlexSizeRule::{kScaleToZero, kScaleToMinimumSnapToZero,
kPreferredSnapToZero, kScaleToMinimum, kPreferredSnapToMinimum,
kPreferred}`; `MaximumFlexSizeRule::{kPreferred, kScaleToMaximum,
kUnbounded}`; `FlexAllocationOrder::{kNormal,kReverse}`). Reuse our
margin/alignment/indent/fixed-size contract for
`kMarginsKey`/`kCrossAxisAlignmentKey` equivalents — do NOT clone
`ui::ClassProperty`; `SetMargin`/`SetAlignment` already exist.

**T5 — DragDropManager.** `Widget` virtuals:
`virtual DragData OnDragBegin(Point)` / `virtual DragOutcome
OnDragEvent(DragPhase, Point)` / `virtual void OnDragEnd(bool
dropped)`; `WidgetTree` owns the active session (state, pointer
capture, hover target, escape-cancel) — generalize
`left_mouse_down_target_` capture. OS bridge (OLE `IDropSource`/
`IDropTarget` on the HWND) is a second phase so files can leave the
window.

**T6 — Transforms.** `Widget::SetTransform(a,b,c,d,tx,ty)` merging
with (not replacing) the CSS-transform path; `FindHitTarget` carries
the inverse accumulated matrix (only translate/scale/rotate → cheap
inverse); `AddDamageRect` maps damage to parent space. Rotate +
perspective stay out unless a need appears (2×3 covers rotate/scale/
translate/skew).

**T7 — Animator.** Value-level `AnimationBuilder` clone scoped to our
model:

```cpp
Motion::AnimationBuilder()
  .SetPreemptionStrategy(kImmediatelySetNewTarget)
  .OnEnded(cb)
  .Once().SetDuration(160ms).SetEase(&EaseOutCubic)
        .SetBounds(widget, r2).SetOpacity(widget, 0.0f)
        .SetStyleProp(widget,"transform","translate(0,8)");
```

Runs off `Window::SetFrameCallback` + `TickTree` today; an
`AnimationAbortHandle` wraps cancel. Maps to widget properties or CSS
props — no layer needed.

## 5. Honest verdict — copy the whole system?

**No — port the contracts, not the machinery.**

Ports cleanly and pays for itself: FlexSpecification drop-out rules +
the FlexLayout builder; focus-reason/ring semantics; Label elision +
measured sizes; a complete KeyCode + kit accelerator registry; the
drag/drop protocol; tooltip anchoring; Tween-driven AnimationBuilder;
per-widget pre/post handlers. These are design decisions encoded in
headers — they compile against our model.

Tied to Skia/Aura — waste to port: `ui::Compositor`/`ui::Layer` tree
(per-widget GPU surfaces and a vsync scheduler buy us nothing over
immediate+ damage on the D2D/Vulkan paths, and they break the
remote-render-worker architecture); aura `DesktopWindowTreeHost` +
`NativeWidget` split + `aura::Window` (cross-platform plumbing for
platforms we don't target); gfx::Transform 4×4 perspective (2×3
suffices); `EventRewriter`/touch/`GestureRecognizer` (no touch
input); `METADATA_HEADER`/`ViewBuilder` property metadata (our CSS +
`xml_ui` already fill the declarative role); `ui::RenderText`
harness (we should wrap DirectWrite/Skia measure behind the
TextMeasurer seam instead); InkDrop *layer* implementation (the
InkDropState **enum** is worth copying — the per-layer ripple
compositing is not; a Tween on `press_` is the same visual).

Biggest semantic gap that is NOT a port detail: views Widgets can be
real top-level HWNDs (POPUP/MENU/TOOLTIP/DRAG) so chrome can exceed
the client rect; our overlay layer is permanently window-clipped.
T11 is the only "architecture-shaped" ticket — everything else is
contract-level.

## Verification notes

- Claims verified against source 2026-11-23: `Label::GetMinSize`
  returns `{0,0}` and `GetPreferredSize` uses a 7.7 px/char estimate
  (`label.cc:71-81`); `KeyCode` enumerates only 13 letters
  (`widget.h:46-61`); `MouseDown` focuses → ring via `IsFocused`
  (`widget_tree.cc:431-433`); paint transforms parse
  `translate`/`scale` only and are ignored by `FindHitTarget`
  (`widget_tree.cc:42-91, 677-700`); no `accessib`/`UIA`/OLE-DnD
  symbols anywhere under `src/ui` (grep). Per wiki convention, later
  landings should add a dated superseding line rather than editing
  these.
