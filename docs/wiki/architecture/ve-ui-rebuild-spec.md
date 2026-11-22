# VE UI rebuild spec — VideoEditor on src/ui/widgets

Design spec for the VideoEditor UI rebuild, 2026-11-22.
**Decision (2026-11-22):** delete `tools/video_editor/ui/` wholesale
(~40k LoC, ~50 files incl. `ui/tools/`) and rebuild the shell purely on
the `src/ui/widgets` kit (74 widgets — button/card/tabs/dialog/table/
sidebar/drawer/scroll_area/resizable/command/context_menu/...). The
engine/model layer (`timeline_model`, `timeline_history`, `compositor`,
`playback/`, `media_catalog`, `ai/`, `lab/`, `photo/`) is untouched.
Verified against the working tree 2026-11-22; external layout claims
carry source URLs.

**Status (2026-11-22):** swap-over DONE — the legacy
`tools/video_editor/ui/` tree was deleted and `ui2/` is the sole UI.
There is no fallback path: the `--ui2` flag and `XER_VE_UI2` env
switch are gone, `RunGui` boots `Ui2::VeApp` directly, and
`--screenshot --ui` captures the ui2 shell. The four widget-kit smoke
checks (`color_widgets`/`data_widgets`/`media_widgets`/`widget_kit`)
moved into `ui2/` unchanged; the `photo_workspace` smoke registration
was dropped — the Photo workspace returns with the ui2 rebuild at M4
(the `photo/` model layer and its `photo_m1/m2/m4` core checks are
untouched). `VeIcons` was ported to `ui2/icon_helpers.{h,cc}`.

Assumed base-API wave (in flight when this spec was written):
`Widget` gains Alignment/Margin/Indent/ScaledContents/sizeHint/Move
and a generic `Surface` paint-callback widget. Where the wave differs,
adjust the surface contract below, not the layout.

## 1. Scope

**Deleted:** everything under `tools/video_editor/ui/` — app shell,
nav rail, monitors, timeline + 10 tool classes, all panels, dialogs,
photo workspace, scopes, waveform/keyframe/title editors, ve_theme,
workspace_layout. Functionality is re-created region-by-region below;
nothing is ported verbatim except the *contracts* (see §6).

**Kept:** every non-UI file in `tools/video_editor/` and all of
`src/ui/`.

## 2. NLE layout survey (2025-2026)

### 2.1 DaVinci Resolve 20 — Edit page
Three regions: left browsers (Media Pool / Effects Library / Edit
Index — tabbed, toggleable half↔full height), Source + Timeline
viewers top-center, tabbed Inspector right, Timeline bottom full
width. Top toolbar = show/hide toggles for each area; page bar at the
bottom edge (Media/Cut/Edit/Fusion/Color/Fairlight/Deliver). R20 moved
keyframes into a dedicated Keyframe/Curve **panel** (no longer buried
in the timeline) and added stacked-timeline tabs (source viewer can
host a second timeline — tab name + playhead turn blue).
Sources: docs.imaster.org DR21 manual ch.40; Resolve 20 New Features
Guide (documents.blackmagicdesign.com).

### 2.2 DaVinci Resolve 20 — Cut page
Media Pool top-left, ONE viewer that toggles source/timeline, **dual
timeline** at bottom (mini whole-sequence strip above the zoomed
working timeline), right Inspector (Video/Sizing/Audio/Effects/
File/Transition tabs), fast-review transport strip.
Source: blackmagicdesign.com/products/davinciresolve/cut.

### 2.3 Premiere Pro 2025
Editing preset: Source Monitor top-left, Program top-right, Project
bin bottom-left, Timeline bottom-right; free dock/tab groups +
workspace presets (Assembly/Color/Effects/Audio/…).
Track headers: track-target buttons (V1/A1…), sync lock, lock, eye
(output), M/S + record-arm on audio, track color/name.
Clips: user label colors + `fx` badge, optional filmstrip thumbs,
keyframe rubber-bands (opacity/volume lines on the clip).
Monitors: timecode readout, playhead ruler + zoom scroll bar, scaling,
resolution menu, safe-margins toggle, export-frame, customizable
button row; Source adds In/Out/Insert/Overwrite, Program adds
Lift/Extract.
Sources: helpx.adobe.com Source/Program Monitor overview (Jan 2026);
peachpit timeline anatomy; ProVideo Coalition track-header notes.

### 2.4 Final Cut Pro 11
Libraries sidebar → browser top-left, single Viewer center, Inspector
right (double-click toggles half/full height), Magnetic Timeline
bottom, toolbar across top with view toggles + timecode HUD; effects/
transitions browsers bottom-right. No fixed track headers — magnetic
storyline + role-colored rounded clips, large audio waveforms,
connected clips above the primary storyline; skimmer + playhead.
Sources: support.apple.com FCP interface/workspace-layout guides;
screencasting.com FCP UI tour.

### 2.5 CapCut desktop
Four zones: asset panel top-left (tabs: Media/Audio/Text/Stickers/
Effects/Transitions/Filters — drag-onto-timeline model), preview
top-center, context-sensitive Inspector top-right (clip props or
project props when nothing selected), timeline bottom. Tool strip
atop the timeline (split/delete/marker/crop/freeze…), snap/magnet +
linkage + fit toggles, Export pill top-right. Track header ≈ hide/lock
only; clips = rounded cards with per-type colored header strips.
Sources: scientificdev.net CapCut interface map; skalablog desktop
tutorial; itechguides panel table.

### 2.6 Clipchamp
Left icon rail → expandable panels (Your media / Record & create /
Content library / Templates / Text / Transitions / Brand kit),
center-top preview with floating select-toolbar, right property rail
(icon → expanding panel), timeline bottom with zoom+/−/fit
bottom-right. Minimal density, consumer-grade.
Sources: support.microsoft.com Clipchamp editor guides;
thurrott.com Windows 11 field guide; clipchamp.com blog (Apr 2025).

### 2.7 Descript
Script editor left (transcript-as-document, scene thumbnails inline),
Canvas/scene editor center (side-by-side/stacked/full toggle),
right sidebar panels (Project/Scene/Layer/Elements/Media/AI
Tools/Underlord), collapsible bottom timeline (layer sizes, storyboard
view, VU meter option, blade/range/slip tools behind "Advanced").
Sources: help.descript.com editor-interface + view-menu; Descript
changelog "the new timeline is ON".

### 2.8 Kdenlive 25.x/26.x
KDDockWidgets free docking; Editing layout: Project Bin top-left,
Clip + Project monitors (tabbed) top-center, Timeline full-width
bottom, effect/composition stack right; classic menu bar + toolbars +
status bar. Track headers: kind icon, name, fx-stack icon, hide, mute,
lock, expand/resize. Monitor: hover-hotspot overlay toolbar (play/ff,
in/out, edit mode, resolution, safe areas) + time ruler + zone +
zoom bar.
Sources: docs.kdenlive.org user_interface/timeline/monitors pages.

### 2.9 Synthesis — cross-NLE conventions

- **Fixed anatomy everywhere:** bin left, monitor(s) top-center,
  inspector right, timeline bottom, workspace switcher + menus on top,
  status/progress at bottom. Deviations are only in docking freedom
  and consumer simplification.
- **Color language:** blue = video clips/track chrome + selection;
  teal/green = audio clips + waveform traces; violet = titles/text
  clips; orange/amber = fx badges/warnings; cyan monospaced =
  timecode; red = record/destructive. Already mirrored in
  `Theme::Tokens` (`tokens.h:52-69`) + `VeTheme` constants.
- **Canvas-drawn:** timeline (ruler, headers, clips, junctions,
  waveforms, playhead), monitor frame + overlays, scopes, meters,
  curve/keyframe editors, photo canvas, transcript word-flow.
- **Widget-composed:** bins, inspectors, toolbars, panels, tab strips,
  menus, dialogs, command palette, status strips. Track headers are
  drawn inside the timeline surface in Resolve/Premiere; Kdenlive uses
  a widget column — see §5 decision.
- **Density:** pro NLEs (Resolve/Premiere/Kdenlive) run 24-28px rows
  and dense chrome; consumer (CapCut/Clipchamp) runs larger targets.
  We target pro density on the existing 4px grid (`kRowHeight=24`,
  `kControlHeight=28` from ve_theme.h → port to token spacing).

## 3. Widget coverage matrix

Legend: **C** = pure composition with stock widgets;
**S** = requires a surface (§6); **G** = gap — needs a new primitive
or a workaround (§4).

| NLE region | Stock widgets | Verdict |
|---|---|---|
| Title bar / menus / workspace switch / ⌘K | `AppTitleBar` + `Command` | C |
| Mode nav rail (Cut/Photo/Lab/Transcript) | `Sidebar` (collapsed) or `ButtonGroup` | C |
| Dock columns + splitters | `Resizable` (fraction sizes, collapse, `SetOnLayout` persist) | C (no drag-docking — G1) |
| Panel tab groups | `Tabs` Pill/Underline + owner-swapped content + `ScrollArea` | C |
| Dock header row (title + ghost actions) | `Label` + `Button` ghost + `DropdownMenu` | C |
| Media bin: tabs/search/list view | `Tabs`, `TextInput`, `Table`, `Item`, `ContextMenu`, `Badge` | C |
| Media bin: thumbnail grid | — (needs `ImageView` or surface thumbs; `Renderer::DrawImage(path,rect,α,r)` exists — projects_widget.cc:137) | G2 |
| Bin folders / layers tree / timeline index | — (`Item`+indent can fake shallow trees) | G3 |
| Program/Source monitor chrome | `Card` chrome + `Button` transport row + `Label` mono timecode + `Select` quality + `Badge` proxy/quality + `ContextMenu` | C |
| Monitor frame + overlays (safe areas, thirds, mask outline, click points) | — | S (MonitorSurface) |
| Timeline: everything (ruler, markers, work-area, headers, clips, junctions, waveforms, playhead, drop ghost) | — | S (TimelineSurface) |
| Timeline tool strip | `Button`/`Toggle`/`Separator`/`Select` + zoom `Slider` | C |
| Scopes (waveform monitor/parade/vectorscope/histogram) | `Chart` is line/area/bar only — not usable | S (ScopeSurface) |
| Audio meters (ballistics, peak-hold) | — | S (MeterSurface — small, embeddable) |
| Inspector property grid | `ScrollArea` + `Accordion`/`Collapsible` + `Field`/`Form` + `Slider`/`Select`/`Combobox`/`TextInput`/`Checkbox`/`Switch` + `Tabs` (Video/Audio/Effects/…) | C |
| Keyframe/curve editor (Resolve-20-style panel) | — | S (CurveSurface) |
| Color wheels + curves mini-editors | `EqEditor` proves the small-canvas pattern | S (inside color panel) |
| Mixer (faders/meters/pan) | `Slider` vertical + MeterSurface; no rotary knob | C+S (G6 knob) |
| History / markers / edit index / export queue | `Table` or `Item` list + `Badge`/`Progress` | C |
| Export dialog, settings, speed, URL import, new layer | `Dialog`/`Sheet` + `Form`/`Field` + selects/inputs | C |
| Effects catalog | `TextInput` search + `Collapsible` groups + `Item` rows (drag source) | C |
| Transcript editor (word flow, selection spans, playhead word) | — | S (TranscriptSurface) |
| Photo canvas (checkerboard/zoom/brush ring/marching ants) | — | S (PhotoCanvasSurface) |
| Shortcuts overlay / status toast | `Dialog`/`Sheet` + `Table` of `Kbd`; `overlays::toast` | C |
| Start screen / project manager | `ProjectsWidget` + `Dialog` | C |
| VST3/CEF plug-views | child HWND plug-view — `Widget::SetVisibleRect` seam (widget.h:214-219) | HWND `Canvas::Canvas` path |

## 4. Missing primitives (gaps)

- **G1 Dock framework** — `Resizable` gives splitters+collapse only;
  no drag-to-dock/tab-tear anywhere in `src/ui` (verified: no
  Dock/Dockable/PanelGroup symbols). Spec scope: fixed regions +
  tab groups + `Resizable` — Premiere-lite. Free docking is a later
  KDDockWidgets-class effort.
- **G2 `ImageView`/`Surface`** — assumed by the base wave; without an
  image widget the bin grid draws thumbs inside a `BinGridSurface`.
- **G3 `Tree` widget** — needed for layers, bin folders, timeline
  index. Interim: `Item` + Indent.
- **G4 Virtualized list/grid** — `Table`/`Item` are one child per
  row; large bins need virtualization or the grid surface.
- **G5 Number-scrub input** — NLE drag-on-value fields (position/scale
  readouts). Interim: `TextInput` + drag handler in Field.
- **G6 Rotary knob** — mixer pan/EQ. Interim: vertical `Slider`.
- **G7 Event-model limits** (`widget.h:27-99`): letter `KeyCode`s only
  arrive with Ctrl held — J/K/L, I/O, V/C/B/N/Y/U/R/H/Z must be
  handled at Window level and routed to the focused surface (the old
  app did exactly this); no `DoubleClick` event (`Item` synthesizes
  its own — surfaces must too); no drag events — bin→timeline DnD is
  a shell-level hit-test handoff (old `on_bin_drop`/`on_drag_hover`
  pattern).
- **G8 2D scroll container** — `ScrollArea` is vertical-only
  (`scroll_area.h:34`); timeline/photo surfaces own their 2D
  scroll/zoom internally (they must anyway).
- **G9 `WaveformWidget` is decorative** — procedural sines, no peak
  data API (`waveform_widget.h:10-12`); real waveforms draw inside
  TimelineSurface/ClipWaveSurface.

## 5. Target layout

Default "Editing" workspace — Premiere/Resolve hybrid, matching the
suite-architecture one-app-with-workspaces decision:

```
┌──────────────────────────────────────────────────────────────────┐
│ AppTitleBar: [workspace ▾] File Edit Timeline View Help │ ⌘K │ L R │
├──┬──────────────────────────────┬───────────────┬────────────────┤
│N │ Left dock (Tabs Pill):       │ Monitors      │ Right dock     │
│a │  Media · Effects · Audio ·   │ ┌───────────┐ │ (Tabs Pill):   │
│v │  Text · Transitions          │ │Source     │ │  Inspector ·   │
│  │  (tab group, Resizable col)  │ ├───────────┤ │  Color · Markers│
│r │                              │ │Program    │ │  History · Mix │
│a │                              │ └───────────┘ │  Queue · Ext · │
│i │                              │  (Resizable   │  Scopes · Trn  │
│l │                              │   or Tabs)    │                │
├──┴──────────────────────────────┴───────────────┴────────────────┤
│ Timeline panel: tool strip ── ruler ── TimelineSurface (full W)  │
├──────────────────────────────────────────────────────────────────┤
│ Status strip: autosave · job progress · sync · GPU/proxy badges  │
└──────────────────────────────────────────────────────────────────┘
```

- Split skeleton: `Resizable` vertical (top row / timeline / status),
  top row = `Resizable` horizontal (left dock / monitors / right
  dock), monitors = `Resizable` horizontal or a `Tabs` group
  (Kdenlive-style tabbed monitors as a view option). Sizes persist via
  `SetOnLayout` → editor config; workspace presets = data
  (`WorkspaceLayout` contract preserved — name + visibility + sizes).
- **Track headers: drawn inside TimelineSurface** (Resolve/Premiere
  convention, scroll-sync for free, matches the deleted widget's
  architecture). The widget-column alternative is rejected for v1 —
  it would need a scroll-synced sibling column the kit can't express.
- Source monitor collapses to a tab (single-viewer mode) for small
  windows — CapCut/Resolve-Cut pattern.
- Right dock `Tabs` use per-tab tooltips (already supported,
  tabs.h:27-32) for the abbreviated labels.

## 6. Surfaces and paint contract

`Surface` (assumed base-wave widget) = in-tree `Widget` whose Render
forwards to `std::function<void(Renderer&, const Rect& content,
const Tokens&)>`; events via the normal `HandleEvent`/`Event` path;
`Invalidate(rect)` gives partial damage (`WidgetTree` already merges
damage rects, `kMaxDamageRects=12`, widget_tree.h:99-124). The HWND
`Canvas::Canvas` (`src/ui/canvas/canvas.h`) is reserved for plug-views
(VST3, CEF) — NodeGraphEditorWidget is the in-tree-vs-HWND precedent.

Paint contract (all surfaces):

1. Paint only inside `content`; snap 1px lines via `Layout::Snap`.
2. No document mutation in Render or HandleEvent — emit hooks; all
   edits funnel through `TimelineHistory`/`PhotoHistory` (one gesture
   = one undo macro, mirroring the deleted ToolCommit pattern).
3. Surfaces own *view state* (zoom, scroll, tool selection); the shell
   owns *document state* and pushes snapshots (`SetProject` model —
   monitor_widget.h:59-66 precedent).
4. Tri-state async media hooks (`MediaLookup::kReady/kPending/
   kFailed`, timeline_widget.h:48-58) — NEVER block paint on decode.
5. Playhead/meter repaint = `Invalidate(playhead_rect)` only, not the
   whole surface.
6. Hit zones computed in Layout-time DIP space; surfaces handle their
   own double-click synthesis and hover cursors.

Surface list:

| # | Surface | Paints | Hooks (shape per TimelineWidget::Hooks precedent) |
|---|---|---|---|
| S1 | TimelineSurface | ruler+labels, marker lane, work-area bar, playhead, track headers (kind bar, eye/lock/M/S/rec, name), clip cards (type-colored header strip, name, thumbs, waveform, fx chip), transition junctions + drag handles, drop-target ghost, link-group rings, in/out flash | select/seek/move/trim/ripple/roll/slip/slide/rate/split/delete/marker/workarea/track-toggles/snap/add-transition/set-transition-duration/add-generated/add-media/get_peaks/get_poster/get_bounds — reuse deleted Hooks struct verbatim |
| S2 | MonitorSurface | letterboxed frame (DrawFrameBgra), safe margins 90/80% + cross, thirds grid, loop band, mask contour, AI click points, zoom/pan; fitted-frame rect exported for hit mapping | on_time_changed, on_click_point, on_toggle_play — chrome (transport/timecode/quality) stays widgets per §5 |
| S3 | ScopeSurface | waveform monitor, RGB parade, vectorscope, histogram — fed composited frame | none (view-only) |
| S4 | MeterSurface | L/R peak bars, ballistics, peak-hold, -12/-3 dB zones — embeddable in monitor edge + mixer | none (SetPeakLevels input) |
| S5 | CurveSurface | keyframe curve editor — Resolve-20-style dedicated panel; easing handles, multi-param rows | on_keyframes(prop, keys) |
| S6 | TranscriptSurface | word flow w/ speaker chips, playhead word highlight, drag-select spans, strikethrough-ignored words, scene thumbs | on_seek, on_delete_range, on_captions, on_marker |
| S7 | BinGridSurface | poster grid (DrawImage), name+meta rows, selection, hover scrub | on_select, on_open_source, on_drag_start (until G2/G4 land) |
| S8 | PhotoCanvasSurface | checkerboard, COW tiles, zoom-at-cursor, pan, brush ring, marching ants, guides | stroke/selection/tool callbacks → PhotoHistory |
| S9 | ColorWheelSurface | hue/sat wheel, temp/tint strips | on_correction |
| S10 | MiniMapSurface (stretch) | whole-sequence overview strip (Resolve Cut dual timeline) | on_scroll_window |

## 7. Theme

Port `VeTheme` constants to the shared `Tokens` NLE fields already
present (timecode/waveform/track_video/track_audio/playhead/
warning_chip — tokens.h:52-69) plus panel/input/border; keep the
`#141417/#1a1a20/#222229/#0e0e12/#2d2d35` ramp and `#22D3EE` accent.
Clip-type colors (video blue, audio teal, title violet, generated
slate, adjustment steel, fx amber) become a small `ve` token extension
or a theme-side helper mirroring `ClipBaseColor`/`ClipTypeColor`.

## 8. Phase plan

- **M1 — shell + docks:** AppTitleBar, Sidebar nav rail, Resizable
  split skeleton, tab-group host, status strip, theme port, workspace
  presets (data), Command palette, toasts, dialog framework,
  Window-level shortcut router (G7). Exit: runnable empty shell,
  `--smoke` harness registered.
- **M2 — monitor + timeline surfaces:** MonitorSurface + transport
  chrome (program first, source second), TimelineSurface + tool strip
  + the Hooks contract bound to model/history/PreviewEngine. Exit:
  edit-parity core — import → timeline → trim/move/split → preview.
- **M3 — panels:** media bin (grid+list), inspector, effects catalog,
  markers/edit index, history, mixer (meters+faders), color,
  transcript panel, scopes, export dialog + queue. Exit: feature
  parity with deleted UI minus workspaces.
- **M4 — workspaces:** Photo (PhotoCanvas + layers/tools panels),
  Transcript (Descript-style split), Lab (object ops on
  MonitorSurface), Extensions/web host. Stretch: MiniMapSurface,
  tabbed monitors option, free docking spike.

## 9. Risks

- **Free docking absent (G1)** — biggest UX delta vs Premiere/Resolve/
  Kdenlive; mitigate via presets + collapsible regions; post-M4 spike.
- **Single-letter shortcuts (G7)** — widget Event can't express them;
  Window-level router must exist from M1 or M2 shortcuts regress.
- **Repaint cost** — timeline/monitor must use region Invalidate;
  whole-surface Invalidate per playhead tick will stall the frame
  budget (WidgetTree already caps/merges damage at 12 rects).
- **DnD** — bin→timeline needs the shell-level drag handoff; there is
  no kit-level drag session.
- **Big-bin performance (G4)** — child-per-row lists will not scale to
  thousands of assets; BinGridSurface or virtualization required
  before M3 sign-off.
- **HWND surfaces vs widget tree** — plug-views (VST3/CEF) need
  `SetVisibleRect` discipline to clip correctly inside ScrollAreas and
  during dock resize.
- **Deleted-UI knowledge loss** — port the Hooks contracts +
  WorkspaceLayout + VeTheme palette BEFORE deleting; they encode ~3
  weeks of verified behavior (JKL ladder, last-active-viewer transport
  rule, exclusive-playback rule, autosave/close guard).

## Sources

- Repo: `src/ui/widgets/*.h` (74), `src/ui/canvas/canvas.h`,
  `src/ui/widgets/widget.h`, `widget_tree.h`, `theme/tokens.h`,
  `tools/video_editor/ui/*` (deletion set) — verified 2026-11-22.
- NLE layout refs in §2 (helpx.adobe.com, blackmagicdesign.com,
  support.apple.com, docs.kdenlive.org, help.descript.com,
  support.microsoft.com, capcut/cursa/skalablog, ProVideo Coalition).
- Related wiki pages: `architecture/suite-architecture.md`,
  `progress/2026-11-21-nle-redesign.md`,
  `research/2026-11-22-modern-nle-features.md`,
  `research/2026-11-21-graphics-editor-stack.md`,
  `progress/2026-11-22-api-max-and-photo-m1.md`.
