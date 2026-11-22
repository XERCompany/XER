# Registry widget survey — color / photo / UX-polish wave

Research for the `src/ui/widgets/` roadmap (~74 widget pairs on disk,
verified `ls` 2026-11-22). Surveys ten shadcn-ecosystem registry sites
for designs worth porting to our native C++ NLE UI kit — color/gradient
pickers, confirm UX, tours, and motion patterns that map onto
`src/ui/motion/` (`Motion::Tween`, `Motion::Spring`, four easings +
`ReducedMotion()`, per `motion.h`). Subagent research; fetched via
`curl -sL` on 2026-11-22; several sites serve `llms.txt` / `.md` docs
which were preferred over scraping HTML.

## 0. Repo baseline (verified this pass)

| Area | What we have | Gap |
|---|---|---|
| Color picking | `tools/video_editor/lab/color_pick.{h,cc}` — `SampleRegionAvg` eyedropper over BGRA frames, `ColorDistance`, hex↔BGRA | **No color-picker widget at all** in `src/ui/widgets/`; no OKLCH anywhere (`src/ui/colorspace/` = color_manager/color_space/hdr_manager only) |
| Gradient | nothing found under `src/ui/` or `tools/video_editor/` | no gradient editor, no conic/radial stops UI |
| Photo edit | `tools/video_editor/photo/` — layer engine: document, layer_ops, adjust_ops, mask_ops, brush, retouch, selection, tile_store, photo_history | engine exists; no dedicated adjustment/curves/histogram/filter-strip **widgets** (`eq_editor` is the only curve-style widget, audio-side) |
| Confirm UX | `dialog`, `alert`, `context_menu`, `switch`, `popover` | no hold-to-confirm, type-to-confirm, undo-window, or consequences list; **no toast/snackbar widget** either (undo-toast pattern needs a host surface) |
| Onboarding | — | no tour/spotlight system, no `data-tour`-style targeting |
| Motion | `src/ui/motion/motion.{h,cc}` + `accel.{h,cc}`: Tween + Spring (semi-implicit Euler, retarget-safe vel) | plenty of headroom — no per-widget motion primitives beyond what widgets hand-roll |
| Audio/agent | `waveform_widget`, `audio_player_widget`, `eq_editor`, `fx_rack_widget`, `message`, `message_scroller`, `bubble` | no live/realtime visualizer, no transcript viewer (whisper output currently lands only as timeline markers), no orb/state viz |

## 1. Colorshot — orshot.com/open-source/colorshot

**What it is:** `@orshot/colorshot` — open-source (React/Vue/headless)
color **and gradient** picker. Store-based: `createPicker()` headless
core shared by the framework wrappers; `parseColor`/`formatColor`
handle the full CSS grammar (`rgb(62 92 235 / 50%)` → `#3e5ceb80`).

**Component / parts list** (compound `Picker.*` API):
`Root`, `Area` (SV field), `Hue`, `Alpha`, `Inputs` (format-switchable),
`Swatches` (grouped, searchable), `GradientBar`, `GradientEditor`,
`GradientControls`, `AngleDial`, `ModeTabs` (Solid/Linear/Radial/Conic),
`EyeDropper`, `Contrast` (contrast readout), `CurrentSwatch`, `Preview`,
`Notice`, `CenterPad`. Plus `ColorField` — a field that opens the picker
in a flipping/shifting popover, closes on Escape/outside-click, returns
focus. Props worth stealing: `modes={"solid"}` to forbid gradients,
`alpha={false}` to forbid transparency, `gradientPresets`,
`onChange` (every frame while dragging) vs `onChangeComplete` (commit
once — where you save).

**Worth porting:** this is the cleanest blueprint for our color panel.
The `onChange`-vs-`onChangeComplete` split maps exactly onto
preview-live / commit-to-undo-stack semantics in an NLE. `AngleDial` +
`GradientBar` + `ModeTabs` covers all three gradient types. `ColorField`
is our `popover.cc` + a trigger — trivial once the picker exists.
Eyedropper: reuse `Lab::SampleRegionAvg` (already does region averaging);
the missing piece is a screen-grab magnifier loop, which for us is
easier than the web EyeDropper API (we own the frame store and the
platform layer).

## 2. Amplo — amplo.ale.design

**What it is:** OKLCH-native, Display-P3-aware fill picker for shadcn,
Base UI + Radix variants, one shared engine. Canonical state is
`OklchColor { l, c, h, alpha }` **unbounded**; all string formats
(hex, rgb, hsl, hsb, oklch, oklab, p3) derived on demand.

**Component / parts list:** `ColorPicker` (solid) + `GradientPicker`
(linear/radial/conic with CSS Color 4 interpolation) + `FillPicker`
(switcher). Parts: `Area` (three modes: `oklch-cl`, `hsv-sv`
Photoshop-style, `oklch-hc` chroma×hue), `Hue`, `Chroma`, `Lightness`,
`Alpha`, `Preview`, `ChannelInput`, `CssInput` (paste any CSS color),
`FormatSwitcher`, `Swatches`, `GamutBadge` (sRGB/P3/out-of-gamut
indicator), `ContrastReadout` (`metrics={"wcag","apca"}`, AA/AAA
badges), `EyeDropper`. Gradient model is fully typed:
`LinearGradient{angle,stops,interp,repeating,start,end}`,
`RadialGradient{shape,center,size,radii}`, `ConicGradient{startAngle,center}`,
`GradientStop{color,position,hint,id}` — the `id` note is real: without
stable stop IDs a controlled picker can't reconcile two stops at the
same position. `softProof` option chroma-reduces out-of-display colors
in OKLCH (hue stays true) instead of per-channel clipping.

**Worth porting:** the strongest single reference for **color engine
architecture**. Recommendation: adopt OKLCH as the picker's canonical
state (perceptually uniform — lightness slider stops lying), with our
existing BGRA/hex grammar as an output format, not the storage. Port:
`GamutBadge` + soft-proof chroma reduction (we have an HDR/color-space
layer in `src/ui/colorspace/` to hook into), `ContrastReadout` WCAG+APCA
(title/safety text over video needs this), `hsv-sv` area mode, `CssInput`
paste-anything, stable stop IDs. Fill-level abstraction
(solid|gradient tabs) matches Fill/Stroke UX in title/graphics tools.

## 3. photocn — photocn.dev

**What it is:** photo-editor components on a WebGL engine (worker
rendering, non-destructive, EXIF-preserving export). `useImageEditor`
headless hook under everything.

**Component list:** `Canvas` (crop frame vs edited result), `Toolbar`
(tool switcher + undo/redo/open/reset/**compare**), `Adjustments`
(light/color/effects sliders), `Filters` (13 presets + strength slider,
0% = original), `Curves` (RGB + per-channel tone curves **drawn over a
live histogram**), `Crop` (rotate, flip, aspect, **straighten**,
perspective — crop by dragging the frame on the photo), `Blur` (lens
blur bokeh/gaussian; drag a dot to pick the focal point), `Blend`
(second image), `Histogram` (live), `Metadata` (EXIF), `Export`
(format/quality/size), `Recipes` (a look as JSON → apply to any photo),
`Command Menu` (⌘K over tools/filters/history), `Layout`/`Compact
Layout`. Blocks: avatar cropper, **before/after** (drag-to-compare),
batch-looks, filter-picker row.

**Worth porting:** direct feed for our `tools/video_editor/photo/`
workspace (engine already there: adjust_ops/mask_ops/photo_history).
Priority designs: (a) **Curves over live histogram** — `eq_editor`
already proves we can draw draggable curves; the histogram is a frame
stat we can compute; (b) **before/after compare** — one draggable
divider, maps to our compositor; (c) **Filters+strength** — preset LUTs
with a 0–100% strength slider (we already ship `assets/luts/` +
`lut_manager`); (d) **Recipes** — JSON look preset, same shape as our
effect presets; (e) focal-point dot for lens blur. Their
non-destructive-edit model (edit list → render on export) mirrors our
`photo_history`/`document` split already.

## 4. SureUI — sureui.com

**What it is:** confirmation components for shadcn/Base UI. Every
control shares one `onConfirm` (may return a promise) and a
`data-state` axis; the core is `useConfirmation` — click-again, hold,
slide and undo behaviors you can put on any element. Their
"choosing a confirmation" doc matches gesture to risk: undo for low,
click-again for medium, hold for high, typed phrase for critical.

**Component list:** `confirm-button` (click / second-click /
press-and-hold + inline undo + pending state), `confirm-menu-item`
(dropdown/context item; **menu stays open while armed/held/pending**,
closes on commit), `confirm-switch` (flips immediately, undoable inside
a window, `onConfirm` fires when the window ends), `type-to-confirm`
(inline form; unlocks only after exact phrase + every acknowledgement
checkbox), `confirm-dialog` (alert dialog hosting either confirm
style; closes only after async onConfirm settles; `useConfirm()` →
awaitable `confirm()`), `confirm-popover` (one-line consequence,
for actions needing a sentence not a dialog), `consequences` (list of
what will be removed, counts, "and N more"), `undo-toast`
(`undoToast(msg)` resolves true on timeout / false on Undo — run the
action only on true), `undoable` (list/table row collapses in place to
label+Undo at same height), `unsaved-changes` (awaitable `confirmLeave()`
+ Save/Discard/Keep-editing footer), `tool-approval` (AI SDK tool-call
approval with risk-matched gesture), blocks: danger-zone, api-keys,
file-manager (trash+undo), inbox, agent-approval, bulk-actions
(escalating confirm as selection grows), settings-save-bar.

**Worth porting:** highest-value non-color find in this survey.
Destructive ops in an NLE (delete clips with ripple, overwrite exports,
clear render cache, delete projects, AI-op overwrites) currently have
only plain `dialog`/`alert`. Port order: `undoable` (in-place collapse
— perfect for media bin / marker lists, needs no toast infra),
`confirm-button` hold variant (pure `Motion::Tween` on a fill ring +
press timer), `undo-toast` semantics (needs a toast surface — a real
gap), `type-to-confirm` + `consequences` for project deletion,
`useConfirm`-style awaitable confirm inside our `dialog`. The
`tool-approval` block also prefigures UX we'll need for agent/AI
features already in-tree (whisper, AI ops, extensions).

## 5. Tour — onboarding-tour.vercel.app (tonghohin/tour)

**Status note:** site returns **402 / DEPLOYMENT_DISABLED** as of
2026-11-22 — docs recovered via GitHub (`tonghohin/tour`, MIT, ~68
stars) and search cache. Single-component registry (`@tour/tour`).

**Design:** `TourProvider` wraps the app with `tours[]`:
`{id, steps: [{id, title, content}]}`. Steps target elements via a
`data-tour-step-id` attribute; `useTour()` exposes `start("main")`
etc. Multiple named tours per app (onboarding, new-feature,
settings-guide), started independently.

**Worth porting:** the *pattern*, not the code — it's thin. For us:
a `Tour` overlay = dimmed backdrop with a spotlight cutout (canvas
region mask), a card anchored to the target widget rect (we have
`widget_tree` for hit-testing and rect lookup — attribute targeting
maps to a `tourId` field or a registry of named anchors), step
sequencer, keyboard nav (Esc/→), progress dots. Motion cost is one
tweened spotlight-rect + card fade — well inside `Motion::Spring`.
Relevant for first-run onboarding and for introducing the Lab/Photo
workspaces. Medium priority; don't build until we have a second tour
to justify the system.

## 6. Delta Components — deltacomponents.dev

**What it is:** a *small curated* set (11 components) by Patrick
Prunty — the "AI + media-rich" framing in the task brief oversells it;
`delta.components.dev` resolves to nothing, `deltacomponents.dev` is
the live site.

**Component list:** `Code` (highlight + copy + filename bar +
package-manager tab strip), `Product Card`, `Editor` (always-on
live-preview markdown — one surface is both rendered doc and input;
marks conceal away from caret), `Tabs` (underline/background/ghost,
shape-following indicator), `Bottom Nav`, `Switch` (double-bounce
thumb, CSS-var geometry), `Button`, `Tooltip` (follow-cursor mode),
`Alert`, `Embed` (URL → preview decided from URL alone), `Image`
(figure + caption + **zoom sized from the picture's own proportions**).

**Worth porting:** selectively. `Image` zoom (hover/click magnify
anchored at pointer) is the desktop-relevant piece — applies to asset
thumbnails and the photo workspace. `Switch`'s double-bounce thumb is
a nice `Motion::Spring` preset (overshoot → settle) for our existing
`switch.cc`. `Tooltip` follow-cursor is a cheap flag on `tooltip.cc`.
`Editor`'s conceal-marks-away-from-caret trick is a genuinely good
idea for a future title/text panel but it's a big lift; note it, don't
schedule it. `Bottom Nav`, `Product Card`, `Embed`: skip (web/mobile).

## 7. CalendarCN — calendarcn.xyz

**What it is:** Notion-Calendar-inspired React calendar:
day/week/**month/year/agenda** views, drag to move **and resize**
events, multi-day events, time zones, .ics import/export, undo toasts,
⌘K command menu, mini-calendar sidebar, settings dialog. Full registry
(`@calendarcn/*`), Tailwind 4 + React 19.

**Honest relevance evaluation:** **low for widgets, medium for
interactions.** We already ship `calendar.cc`; a scheduling UI is not
an NLE's job — nobody books meetings inside Resolve. What *is* ours:
(a) **drag-to-move/drag-to-resize on a time grid** — that interaction
vocabulary (edge-grab resize, multi-day spans, snap) is literally the
timeline's; their day/week grid is the closest web analogue to our
timeline track area; (b) **undo toasts after delete/cut/copy** — same
SureUI undo pattern; (c) year view's density-dot marking maps to our
`marker.cc` idea-space (e.g., days-with-renders, days-with-edits in a
project-activity view); (d) the ⌘K "go to typed date" pattern →
"go to timecode/frame" in our `command.cc`. Do **not** port the month
grid — ours exists. Steal the interaction model and the undo toast.

## 8. Magic UI — magicui.design (~80 components; landing-page oriented)

Scanned the full index; most is web-marketing decor (device mockups,
tweet cards, meteors, retro grids — skip). Desktop/NLE-relevant subset:

| Component | What it is | Map to |
|---|---|---|
| `number-ticker` | count up/down to target | `Motion::Tween` on a displayed double → stats, export progress, timecode-ish readouts |
| `dock` | macOS-style magnifying icon bar | Spring on icon scale by cursor distance → tool rail / workspace switcher |
| `ripple` / `ripple-button` | expanding rings; click-origin ripple | Canvas-drawn ripple on `button.cc` / ink-well effect |
| `animated-list` | stagger-in items | per-item delayed Tween → notifications, render queue |
| `blur-fade` | blur+fade entrance | entrance preset for dialogs/cards |
| `magic-card` | spotlight border follows cursor | hover glow on `card.cc` — shader-free radial highlight |
| `lens` | pointer-anchored image zoom | same as Delta `Image` — asset thumb loupe |
| `progressive-blur` | edge fade on scroll containers | `scroll_area.cc` edge treatment (we have `scroll_area`) |
| `marquee` | infinite scroll | credits/ticker only — low value |
| `scroll-progress` | scroll-position bar | `scroll_area` chrome |
| `file-tree` | folder tree | we have `projects_widget`; only if missing tree UX |
| `terminal` | typed terminal block | maybe for render-log surface — low |
| `confetti` / `cool-mode` | celebration particles | render-complete delight — cheap, optional |
| `shine-border` / `shimmer-button` | animated border/perimeter light | "agent is thinking" affordance on AI panels — moderate |
| `typing-animation` / `hyper-text` | typewriter/scramble text | streaming-text surfaces for agent features |
| `highlighter` | marker-stroke highlight | text annotation; low |

**Skip:** globe, icon-cloud, warp-background, video-text, android/
iphone/safari mockups, tweet cards, bento-grid, hero-video-dialog,
dither-field, floating-3d-particles, dotted-map, word-rotate,
scroll-based-velocity (all web-scroll-dependent or marketing-only).

## 9. Animate UI — animate-ui.com

**What it is:** animated drop-in primitives for Radix/Base UI/React
Aria, built on Motion (the library our `Motion` namespace parallels —
same spring+tween model). The *primitives-over-primitives* packaging
(animate once in the shared base, every widget inherits) is itself the
most portable idea here.

**Own primitives (the interesting half):** `auto-height` (container
tweens its height on content change), `highlight` (one gliding
background between items — tabs/menus), shared-`tooltip` (one popup
that *moves* trigger-to-trigger instead of close/reopen), `counting-
number`, `sliding-number` (odometer digits), `scrolling-number`,
`click` (ring/ripple/crosshair/burst at pointer), `ripple-button`,
`image-zoom`, `magnetic` (element pulled toward pointer), `tilt` (3D
card tilt), `spring` (draggable, springs back on release — literally
our `Motion::Spring` + drag input), `splitting/morphing/rolling/
rotating/swapping/sliding-text`, `typing-text`, `effect/fade/blur/
slide/zoom` entrances, `pinned-list`, `questionnaire` (multi-step;
height glides, slides exchange), `scroll-fade` (edge fades only where
more content exists), `motion-grid` (dot grid frame-by-frame status),
`avatar-group`, `copy-button` icon swap, `code-block` self-typing.
Plus animated versions of every standard primitive (accordion height,
dialog scale+fade, drawer drag, tabs indicator glide, etc.).

**Worth porting (motion-system level, not widgets):**
- **Entrance preset family** (fade/blur/slide/zoom + combined `effect`)
  as named `Motion` recipes so widgets stop hand-rolling.
- **`auto-height`** — animating layout height is the single most common
  "feels cheap" gap in native UIs; a tweened preferred-height on
  `collapsible`/`accordion` we already have.
- **`highlight` glide** — one indicator that slides between
  tabs/menu items/segmented controls; we have `tabs`, `button_group`,
  `toggle` groups to hang it on.
- **Shared moving tooltip** — our `tooltip.cc` reopens per widget;
  a provider-level popup that translates between anchors is a small,
  high-polish change.
- **`questionnaire` height-glide** — directly reusable for wizard/
  setup dialogs.
- **`click` effects & `sliding-number`** — pointer feedback and
  odometer readouts; cheap, fun, on-brand for an editor.
- `spring` drag-back is a demo of something our Spring already does;
  cite it as validation, not a feature.
- **Skip:** magnetic, tilt, page-transition, theme-toggler (view-
  transitions API has no native analog worth faking), morphing text.

## 10. ElevenLabs UI — ui.elevenlabs.io

**Status note:** site 403s non-browser clients (Vercel checkpoint);
component list recovered via `github.com/elevenlabs/ui` README and the
index mirror. MIT, 62 registry items total, but the base set is a
re-export of shadcn — the **17 domain components** are the signal:
`Orb` (voice-agent state orb — idle/listening/speaking), `Waveform`
(static), `Live Waveform` (real-time mic/TTS), `Bar Visualizer`,
`Matrix` (dot-matrix visualizer), `Audio Player`, `Scrub Bar`,
`Mic Selector`, `Voice Picker`, `Voice Button`, `Speech Input`,
`Transcript Viewer`, `Conversation` + `Conversation Bar`, `Message`,
`Response`, `Shimmering Text` (+ streaming text, prompt input).

**Worth porting:** we overlap more than expected — `waveform_widget`,
`audio_player_widget`, `message`, `message_scroller`, `bubble` already
exist. The deltas worth taking:
- **`Live Waveform` / `Bar Visualizer`** — real-time-input waveform
  (ours is file/static). Needs an audio-level tap on the input stream;
  we own `src/audio/` so this is an engine hook + a rendering mode on
  `waveform_widget`, not a new widget.
- **`Orb`** — compact agent-state indicator (idle/recording/thinking/
  speaking) for the AI features already in-tree (whisper, lab AI ops,
  extension agents). One widget, high identity value.
- **`Transcript Viewer`** — word/segment-aligned transcript with
  click-to-seek. Our `whisper_transcribe` emits `TranscriptSegment
  {t0,t1,text}` but lands only as timeline markers (per
  `modern-nle-features.md`: no text-editing surface). This is the
  missing UI for text-based editing — **the sleeper pick of this
  survey**.
- **`Scrub Bar`** — dedicated drag-to-scrub strip; cheaper than a full
  timeline for preview/audio panels (we have `slider` but a scrub bar
  has hover-time-preview semantics).
- **`Conversation`/`Response`/streaming text** — for a future agent
  panel; `shimmering-text` as "generating" state.
- Skip: voice-picker, mic-selector (we'd bind our own device enum),
  eleven example blocks (SDK-specific).

## 11. Cross-cutting takeaways

1. **OKLCH canonical + gamut/soft-proof is the modern color-picker
   architecture** (Amplo); Colorshot is the better *interaction*
   reference. Build one engine on OKLCH, UX patterned on Colorshot's
   parts (`ModeTabs`/`GradientBar`/`AngleDial`/`ColorField`), and reuse
   `lab/color_pick.cc` for the eyedropper's sampling half.
2. **Confirm UX is a *risk-matched gesture vocabulary***, not a widget:
   undo < click-again < hold < type-phrase. One shared `Confirmation`
   helper + per-surface adapters (button, menu item, switch, row,
   toast) — exactly their `useConfirmation` shape.
3. **Undo needs a toast surface we don't have.** `undoable`'s in-place
   collapse dodges that dependency; the toast itself is worth building
   anyway (render-complete, autosave, project events all want it).
4. **Animate UI's lesson is architectural**: put motion *in the
   primitive* (accordion height, tab indicator, tooltip relocation)
   once, and every consumer inherits it — matching our design note in
   `motion.h` that entrances/exits/transforms are already policy.
5. **Transcript Viewer is the highest-leverage "audio" component** —
   we already own the hardest part (whisper in-tree emitting
   timestamped segments); the web lib assumes a paid SDK, ours is free.
6. CalendarCN: don't port the calendar; port the drag/resize +
   undo-toast *interactions* onto timeline and activity views.

## 12. Prioritized recommendations

| # | Port | Source | Effort | Value | Notes |
|---|---|---|---|---|---|
| 1 | `color_picker` widget: SV area + hue/alpha sliders + format inputs + swatches; OKLCH canonical state | Amplo engine + Colorshot parts | L | **Critical** | no color widget exists at all; gates everything below |
| 2 | `gradient_editor`: ModeTabs (solid/linear/radial/conic), GradientBar stops w/ stable IDs, AngleDial | Colorshot + Amplo gradient model | M | **Critical** | titles, mattes, generated sources all need it |
| 3 | `eyedropper` mode on the picker: magnifier loop → `Lab::SampleRegionAvg` | Colorshot `EyeDropper` part | M | **Critical** | sampling half already exists in `color_pick.cc` |
| 4 | Confirmation core: shared gesture helper (click-again/hold/type) + `confirm-button`, `confirm-menu-item`, `type-to-confirm`, `consequences` | SureUI | M | High | destructive-op parity across bin/timeline/project |
| 5 | `undoable` row + undo-window semantics; `toast`/`undo-toast` surface | SureUI (+CalendarCN pattern) | M | High | toast surface is itself a missing widget |
| 6 | `transcript_viewer`: segment list, click-to-seek, word highlight | ElevenLabs `Transcript Viewer` | M | High | whisper already emits segments; unlocks text-edit later |
| 7 | Photo-workspace widgets: `curves` over live `histogram`, `filter_strip` (LUT+strength), `before_after` compare | photocn | M–L | High | engine (`photo/`, `lut_manager`, `eq_editor` pattern) already there |
| 8 | Motion presets: entrance family, `auto-height`, `highlight`-glide indicator, shared moving tooltip | Animate UI | S–M | High | one-time motion-system upgrade, benefits all widgets |
| 9 | `gamut_badge` + soft-proof chroma-reduction + `contrast_readout` (WCAG/APCA) | Amplo | S | Medium | correctness + title-safety over video |
| 10 | `orb` agent-state indicator; `live_waveform` mode on `waveform_widget` | ElevenLabs | S–M | Medium | agent UX identity; needs an audio-input tap |
| 11 | `number_ticker`/`sliding_number` readouts; `ripple` click feedback; `progressive-blur` scroll edges | MagicUI + Animate UI | S | Medium | polish pass on existing widgets |
| 12 | `image_zoom`/loupe on asset thumbnails | MagicUI `lens`, Delta `Image` | S | Medium | one pointer-anchored zoom path, shared |
| 13 | `tour` spotlight overlay + named anchors on `widget_tree` | @tour/tour | M | Medium–Low | build when a second tour exists |
| 14 | `switch` double-bounce spring preset; tooltip follow-cursor flag | Delta | XS | Low | one-line-ish tweaks |
| 15 | Dock, marquee, confetti, device mockups, calendar month grid, magnetic/tilt/morphing text | MagicUI/Delta/AnimateUI/CalendarCN | — | **Skip** | web/marketing-only or already covered |

**Skip summary:** ~60% of MagicUI (mockups, decorations, scroll-jacked
text), CalendarCN's calendar proper, Delta's bottom-nav/product-card/
embed, ElevenLabs' base-set re-export and SDK-bound pickers, all
view-transitions/page-transition machinery.
