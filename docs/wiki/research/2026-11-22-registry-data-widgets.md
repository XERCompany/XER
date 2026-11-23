# Registry data-widgets survey — shadcn registries → XER widget kit

Date: 2026-11-22. Research only — no code changes.

Surveyed 12 shadcn-registry sites for widget designs worth porting to the
native C++ kit in `src/ui/widgets/` (~72 widget types). Focus domains for the
NLE: panels, bins, tables, inspector.

## Our kit — inventory snapshot

`src/ui/widgets/` (h+cc pairs, ~72 types):

- **Primitives**: button, button_group, toggle, checkbox, radio_group, switch,
  slider, select, combobox, text_input, textarea, input_otp, input_group,
  field, form, label, kbd, badge, avatar, separator, skeleton, spinner,
  progress, aspect_ratio, direction, item, marker.
- **Overlay/nav**: dialog, drawer, sheet, popover, hover_card, tooltip,
  dropdown_menu, context_menu, command, tabs, accordion, collapsible,
  breadcrumb, pagination, sidebar, app_title_bar, carousel, alert, empty,
  bubble, message, message_scroller, attachment.
- **Structure**: card, surface, resizable (draggable split panels, fractions,
  collapse, `SetOnLayout`), scroll_area, table (string cells, header sort flag,
  manual scroll — no virtualization/resize/typed cells), widget, widget_tree
  (render infra — NOT a user-facing tree view).
- **Domain widgets**: chart, calendar, eq_editor, fx_rack_widget(+state),
  node_graph_editor_widget, node_param_host, chain_map_widget,
  audio_player_widget, video_preview_widget, waveform_widget, projects_widget,
  settings_widget, security_flash_panel_widget, screen_loading_widget.

API conventions (from `table.h`, `slider.h`): `static std::shared_ptr<X>
Create()`, `SetX`/`GetX`/`SetOnEvent(cb)` with `std::function`, lifecycle
`Render(renderer, tokens)` / `Layout(bounds)` / `Tick(dt)` / `HandleEvent`,
sizing via `GetMinSize/GetPreferredSize/GetMaxSize`. `widget.h` KeyCode enum
already covers arrows/Home/End/PageUp/Down/Escape/Tab/Enter/Space/Backspace.

**Confirmed gaps**: user-facing TreeView (G3), virtualized data table (G4),
Figma-style scrub number field (G5). Secondary: tags input, stepper,
virtual list substrate, faceted filter bar, file dropzone, inline edit,
bulk-action bar, histogram range, unit/duration inputs, inspector composition,
gantt, kanban.

**Already covered**: dockable split (`resizable`), flow diagram
(`node_graph_editor_widget`), message/conversation set, most shadcn
primitives.

## Layout primitives (layout.csikie.com — "Layout Kit")

Stack/Row/Col/Grid/GridItem/Page with *typed responsive props*:
`cols={{base:1, sm:2, lg:4}}`, `gap={6}`, `Page.Header/Page.Body` compound
slots. Verdict: our `layout/flex.h` is lower-level (AlignMode + rect helpers,
no container widgets). Worth having Row/Col container widgets with typed
`gap`/`align` props eventually, but NOT urgent — panels already hand-layout.
Low priority.

## 1. drift-ui.dev — "the components shadcn left out" (64 items)

Machine-readable index via `/llms.txt`, full per-prop docs via
`/llms-full.txt`. Categories: 29 inputs, 14 structure, 13 workflow, 8
primitives. Relevant items (full catalog: tags-input, stepper, tree-view,
file-dropzone, virtual-list, filter-builder, saved-views, bulk-action-bar,
sortable-list, inline-edit, description-list, histogram-range, unit-input,
duration-input, number-input, time-field, masked-input, multi-select,
key-value-editor, json-viewer, diff-viewer, csv-importer, kanban-board,
comment-thread, log-viewer, trace-waterfall, transfer-list, query-input,
color-picker, cron-input, recurrence-editor, date-time-range, timeline,
notification-inbox, onboarding-checklist, tour, toast+undo, emoji-picker,
permission-matrix, rating, signature-pad, activity-heatmap, uptime-bar,
address/phone/timezone/password/secret/template/mention inputs,
weekly-hours, shortcut recorder/cheatsheet).

### TreeView — THE G3 reference (drift-ui.dev/r/tree-view.json)

```ts
type TreeNode = { id: string; label: ReactNode; icon?; children?: TreeNode[];
                  isBranch?; disabled?; textValue? }
<TreeView items={nodes}
  expanded defaultExpanded onExpandedChange       // controlled/uncontrolled
  selected defaultSelected onSelectedChange       // string | string[] (multiple)
  multiple                                        // checkbox mode, branch => aria-checked="mixed"
  loadChildren={(node)=>Promise}                  // lazy: isBranch w/o children => loading row; reject => Retry row
  virtual                                         // fixed 32px rows, overscan, focus survives scroll
/>
```

Interactions: arrows move/expand, Home/End jump, Enter/Space select, typing
does typeahead; ArrowRight on a lazy branch retries a failed load. Lazy
children appended into `items` before the promise resolves. Perf claims:
virtual 100k open rows ≈ 17 ms expand, <10 ms scroll; DOM cap ~520k rows.
Checked branches auto-check children loaded later. Port notes: matches our
`SetItems/GetSelected/SetOnX` conventions exactly; `virtual` shares the
substrate with the G4 table work.

### Stepper (drift + francozeta-stepper.vercel.app)

Drift: Tabs-backed, zero-based `value/onValueChange`, `linear` disables steps
past furthest reached, compound parts (List/Item/Trigger/Indicator/Title/
Separator/Content/Previous/Next, `stepCount`, `useStepper()`).
Francozeta: headless typed state machine, "bring your own UI, keep the state
machine predictable" — guards on transitions. Port: one `Stepper` widget
(step list + active index + linear flag) + indicator rendering; guards =
`CanAdvance` callback. Export wizard use-case.

### TagsInput

`value/onValueChange string[]`, `max` (field read-only when full, tags still
removable), `validate(tag)`, `allowDuplicates`, `addOnBlur`. Enter/comma add;
Backspace/ArrowLeft at caret-start selects last tag, second Backspace
removes; paste splits on `,\t\n`. Bin keyword/metadata fields use-case.

### FilterBuilder — faceted filters for bins

`fields: {id,label,type:text|number|select|date,options?}[]`, value is
`Filter[] | FilterGroup` (nested AND/OR, ≤2 levels recommended), plus lib
fns `matchesFilters(row,filters)`, `serializeFilters`/`parseFilters` (compact
URL-safe DSL). Rendered as removable chips. Pair with openstatus faceted
counts (below).

### VirtualList — G4 substrate

`items: T[]`, `estimateSize` (px guess until measured), `children(item)=>row`,
`overscan=4`, `getItemKey` (stable identity → measured heights + focus follow
the row, not the index). Variable heights self-measured; focused row stays
mounted off-screen; one tab stop; arrows/PageUp/PageDown/Home/End navigate
unmounted rows. This is the reusable engine under both TreeView(virtual) and
a virtualized Table.

### Other worth noting

- **HistogramRange**: dual-thumb range slider under a distribution histogram —
  "filter by duration/size" in a bin. `data[]`, `value:[lo,hi]`, `formatValue`.
- **UnitInput / DurationInput**: value stored in base unit, unit picker,
  typed "2gb"/"1:30" shorthand. Merge-worthy with scrub field (bitrate,
  frame-rate, memory settings).
- **BulkActionBar**: floating toolbar over multi-select, Esc clears. Bin ops.
- **InlineEdit**: click-to-edit-in-place, Enter saves / Esc cancels / async.
  Clip renaming.
- **SortableList**: drag reorder + keyboard. FX-rack/track ordering.
- **DescriptionList**: key/value rows + copy buttons + inline edit — half of
  an inspector.
- **KeyValueEditor**: env-var style rows — render settings dicts.
- **SavedViews**: named view configs (sort/filter/columns) for bins.

## 2. kinetic.itsjay.in — ScrubNumberField (G5 reference, MIT)

Figma-like drag-on-value input. Full prop list:

```ts
value defaultValue onValueChange onValueCommitted   // committed = blur/scrub-end
min max step smallStep(Alt,def .1) largeStep(Shift,def 10)
defaultResetValue                                   // double-click logo resets
direction: "horizontal"|"vertical" pixelSensitivity // px per step, def 2
allowWheelScrub boundFeedback:"none"|...            // edge feedback at min/max
format: Intl.NumberFormatOptions                    // display formatting
```

Interactions: drag scrubs (cursor lock optional); Alt=fine, Shift=coarse;
click = text edit (shows full value, not clipped display); Up/Down nudge
(+modifiers); Home/End = min/max; Enter/Escape commit/revert; wheel scrub
opt-in. Digit-roll animation (Calligraph) is polish — our `Motion::Tween`
can approximate. Internal lib splits cleanly: `scrub-number-math`
(quantize/clamp/step-resolve/modifier-override/pointer-delta/threshold) +
overflow display. **This is a direct blueprint** — port the math module
1:1, it's plain functions.

## 3. ui.emstein.com — virtualized Table (G4 reference, MIT)

Single-component registry; TanStack Table + TanStack Virtual.

```ts
type EmsteinUITableColumn<T> = { key, label,
  type: "longText"|"status"|"currency"|..., align? }
<Table data columns getRowId={(row)=>row.id} selection={{}} />
```

Features: virtualized rows (container or page scroll), click-header sorting
(initial column or unsorted start), column resize where a column *trades
width with its neighbor* (total stays constant — good fit for fixed-width
panels), opt-in checkbox selection column (controlled/uncontrolled, keyed by
row id), per-type width presets + grow factor for leftover space, footer
summaries computed over sorted rows. Our `table.cc` is the naive version of
exactly this; port = upgrade Table onto the VirtualList substrate + typed
cell renderers (`std::function<void(Renderer&, row, col, rect)>`) + resize.

## 4. gridcn.vercel.app — editable DataGrid (Apache-2.0, v2.0)

Excel-class editing, deliberately scoped: **no** grouping/tree/pivot/
master-detail. Core: `defineColumns<T>()([{id,header,accessorKey,type:
"text"|"number"}])`, `defaultData`. Interactions: range selection
(anchor+drag handles), clipboard paste from Excel (TSV), fill handle
(drag corner → series continuation 4,5,6…), Ctrl+Z undo/redo.
Server mode: `useDataGridServerRows({fetchRows({spec,start,end,signal})})` —
grid sends sort/filter/search spec + row window; `sortMode/filterMode:
"server"`; selection across pages by row id; edits save with rollback.
Add-ons as separate installs: fill handle, undo, find&replace,
import/export, file-dropzone, URL state, saved views, pinned rows+totals,
presence, context menu, keybindings, status bar, toolbar+search, sort list,
client pagination, lookup cell. Port relevance: range-select + TSV
clipboard + fill handle are NLE-spreadsheet features (marker lists,
shot tables, metadata editing). Modular add-on split is a good model —
core grid stays lean.

## 5. data-table.openstatus.dev — faceted-filter data table

Key idea worth stealing: **one `createTableSchema` definition drives
everything** — columns, filter controls (faceted: each option shows its
count), row-detail sheet, server query handler, NL filter parser, MCP
schema. Features: faceted filters w/ counts, sorting, cursor pagination +
infinite scroll, virtualization, URL state (nuqs), ⌘K command palette,
row-click → detail Sheet, column-visibility, `useMemoryAdapter` (client
mode), `DataTableAuto`/`inferSchemaFromJSON` (schema from raw JSON),
`DataTableRemote` (endpoint serves a manifest), row actions column +
actions bar. For us: the single-schema concept maps to a
`TableModel`/`BinModel` driving columns+filters+inspector fields together;
faceted counts = "Codec: H.264 (142), ProRes (38)" in bin filters.

## 6. keyline-ui.dev — dense work-heavy UI (75 items, Base UI)

The most design-transferable site here — it's basically an NLE aesthetic
instrument panel. Three stated laws:

1. **Related controls touch** — a field and its unit, a button and its
   count share one frame/one seam; unrelated groups a full gap apart.
2. **Line weight shows role** — 3 px region, 2 px control, 1 px inter-cell.
3. **State stays in place** — selection/error/done fills its own cell;
   nothing else moves.

Everything is "cells on shared keylines" — maps cleanly onto our
`Theme::Tokens` hairline model and SnappedRect pixel snapping. Components:
Toolbar (one Tab stop, cells on shared seams, exclusive toggle group with
stretching ink band, docked menus, embedded input cell), Menu/Menubar/
ContextMenu (code cells, shortcut cells, ink cursor), **Inspector**
(non-modal contextual panel: header cites the object's code/id — "the two
share an address" with the list row, InspectorFacts key/value grid,
InspectorSection rows, sticky InspectorActions footer), Panel+PanelGrid
(region frame; panels join on shared keylines), Resizable (1px seam → 3px
ink on hover/drag, grip cell, keyboard collapse — same react-resizable-
panels model as ours), DataTable (TanStack state + Keyline parts:
`useDataTable({data,columns,defaultState})`, `DataTableColumnDef` with
`accessorKey/header/cell/enableSorting/sortDescFirst/meta.align`,
TableFrame/TableToolbar/DataTableSearch/DataTableBulkBar/
DataTablePagination composition), Segmented, Stat, Nav (rail + groups +
row actions — we have sidebar), AppShell, Item (media/content/meta/action
cells — our `item`), Questionnaire (multi-step form = stepper cousin),
Shipments block (list + filterable table + bulk undo + inspector +
⌘K + toasts — literally a bin panel). Port the *composition language*
(cells/seams/one-tab-stop toolbar/inspector-header-cites-row-id) more than
individual widgets.

## 7. panelui.dev/docs/web — 84 components

Animated data-rich set. Relevant names: **KPI** tiles, **Lane Board**
(kanban), **Flow** (node diagram — we have node_graph_editor), **Inspector**,
**Dock**, **Toolbar**, **Workbench**, **Table**, Steps, Stat, Range Meter
(gauge), XY Pad (2D control — useful for audio/color params!), Rolling
Number (odometer digits — same polish as Calligraph), Soundwave,
CurrencyInput, Day Grid, Scroll Header, Nav Rail, Charts (area/bar/bubble/
bump/candlestick/funnel/heatmap/line/live/pie…). Takeaways: KPI tile +
Lane Board for render-queue/dashboard surfaces; **XY Pad** is a genuinely
new input type worth noting (2-axis drag → e.g. EQ freq/gain, pan); Range
Meter for levels.

## 8. ui.ilinxa.com — 64 pro components, 9 categories (llms.txt catalog)

Relevant: **gantt-timeline** (editable bars, collapsible summary rows,
milestone diamonds, continuous zoom hours→quarters, today line),
**kanban-board** (swimlanes, per-column rules, card-renderer registry),
**file-manager** (Finder-style: grid+list views, marquee multi-select,
cut/copy/paste, drag-drop, shared clipboard — this IS a media bin),
**file-tree** (VS Code tree: icons, CRUD, DnD, lazy children, multi-select),
**event-calendar** (month/week/day/agenda, drag+resize editing),
**media-library** (Drive-style folders, lazy load, drag-drop upload/move,
multi-type preview), **task-tree** (multi-select, bulk ops, search/filter
toolbar, dual DnD, virtualization), **split-workspace** (splittable/
mergeable editor areas — our resizable + tabs), **detail-panel**
(selection-aware, read/edit modes, lifecycle states, sticky header/footer —
inspector again), **properties-form** (schema-driven typed-record form —
node_param_host territory), **filter-bar** (search + category pills +
date-range + count), **filter-panel** (schema-driven checkboxes/toggles/
text, AND-composition, debounced), **data-table**, **flow-canvas** (typed
ports — our node editor). Confirms: bin = file-manager shape; the
tree+table+filter+inspector composition is universal.

## 9. kibo-ui.com — 41 composable components

Index: Gantt, Kanban, Calendar, List, Table (project mgmt set); Dropzone,
Choicebox, Tags, Mini Calendar (forms); Color Picker (Figma-style), Image
Crop/Zoom, Editor, Tree, Dialog Stack, Timeline, Status, Pill, Rating,
Relative Time, QR, Ticker, Stories/Reel/VideoPlayer; AI Elements, Wheel
Picker. **Gantt** is the notable one: resizable+draggable bars, markers,
today line, create-marker trigger, feature grouping, **multiple items per
row** (resource scheduling), optional sidebar, read-only mode. Gantt ≈ our
timeline's distant cousin — useful pattern for render queue / resource
scheduling, not a clone of the NLE timeline itself. ReUI also demos Gantt
dependency links (shared trunk lines, dashed broken links).

## 10. reui.io — component index

Components: Alert, Autocomplete, Badge, Cascader (multi-level picker —
new), Code Block, Color Picker, Data Grid, Date Selector, Emoji Picker,
Event Calendar, File Type, File Upload, Filters, Frame, Gantt (w/
dependency links), Icon Stack/Tile, Kanban, Number Field, Phone Input,
Rating, Scrollspy, Signature Pad, Sortable, Stepper, Time Picker,
Timeline, Tree. Plus 687 pro blocks: App Shell 36, Data Grid 70 (grouped
subcategories: columns 5, DnD 4, editing 6, expansion 3, filtering 4,
grouping 7, virtualization 2), Gantt 4, Kanban 10, Wizard 7, Flow 4,
Whiteboard 2, Timeline 9, Schedule 10. Mostly block-level compositions;
confirms demand ranking (data-grid variants dominate the catalog).

## Cross-cutting patterns to adopt

- **Controlled/uncontrolled duality** everywhere (`value`+`onValueChange` OR
  `defaultValue`): our Set/Get pattern already does this implicitly — keep it.
- **Compound-part naming** (StepperList/Item/Indicator, InspectorHeader/
  Facts/Section/Actions): in C++, expose `struct` config + `AddSection`
  methods rather than child widgets.
- **Single schema driving table+filters+inspector** (openstatus,
  ilinxa properties-form): our `Table::Column` should grow `key`, `type`,
  and the inspector should be able to render the same field descriptors.
- **Row-id keyed selection** survives sort/filter/virtualization — always
  key selection by id, never index (Emstein, gridcn, drift tree all do).
- **Cells-on-shared-seams density language** (Keyline): 3/2/1 px line-weight
  hierarchy, related controls share a frame, state fills its own cell —
  already compatible with our pixel-snapped token rendering; adopt as a
  documented density mode rather than new widgets.

## Prioritized port table

| # | Widget | Proposed file | API sketch | Effort | NLE use-case |
|---|--------|---------------|------------|--------|--------------|
| 1 | ScrubNumberField | `scrub_number_field.h/.cc` | `SetValue/GetValue/SetOnValueChanged/SetOnValueCommitted; SetRange(min,max); SetSteps(step,fine,coarse); SetPixelsPerStep; SetResetValue; SetFormat; drag=scrub, click=edit, Alt/Shift modifiers, Home/End, dbl-click icon reset` | S–M (2–3 d) | Every inspector param: position, scale, opacity, volume, speed — Figma/Nuke-style drag-on-label |
| 2 | TreeView | `tree_view.h/.cc` | `struct TreeNode{id,label,icon,children,is_branch,disabled}; SetItems; SetExpanded/GetExpanded; SetSelected (+multi w/ tri-state); SetOnExpand/Select; SetLoadChildren(cb); SetVirtualized(bool)` | M (3–5 d) | Project panel bin hierarchy, effects presets, sequence/markers outline — gap G3 |
| 3 | VirtualList | `virtual_list.h/.cc` | `SetItems(count); SetEstimateHeight; SetRowHeightCb/MeasureCb; SetRenderRow(cb(row,rect)); SetOverscan; ScrollTo(row); key-stable` | M (3 d) | Substrate for #2+#4; bins with 10k+ assets, marker lists, log views |
| 4 | DataTable (upgrade `table`) | extend `table.h/.cc` | `Column{key,header,type,width,grow,resizable,sortable}; typed CellRenderer cb; SetRows(model) w/ row ids; SetOnSort(col,dir); header drag-edge resize (neighbor-trade); checkbox selection col; virtual rows via #3` | L (5–8 d) | Bin list view, render queue, marker table, process_task table upgrade — gap G4 |
| 5 | TagsInput | `tags_input.h/.cc` | `SetTags/GetTags/SetOnTagsChanged; SetMax; SetValidate(cb); SetAllowDuplicates; SetAddOnBlur; paste-split ,/\t/\n; Backspace select-then-delete` | S (1–2 d) | Clip keywords/labels, bin metadata, export preset lists |
| 6 | FilterBar (faceted chips) | `filter_bar.h/.cc` | `SetFields({id,label,type,options}); SetFilters/GetFilters (Filter list, AND/OR); SetOnFiltersChanged; per-option counts; chip render+remove` | M (3 d) | Bin filtering (codec/duration/rate facets), marker search |
| 7 | FileDropzone | `file_dropzone.h/.cc` | `SetAccept(globs); SetOnDrop(files); states: idle/hover-valid/invalid/progress; click→file dialog` | S–M (1–2 d) | Media import into bins, project panel drop target |
| 8 | Inspector | `inspector.h/.cc` | `SetHeader(code,title,status); AddFacts({label,value}); AddSection(title,widget); SetActions(widgets); sticky footer; linked to list selection by shared id` | S (1–2 d) | Properties/effects panel paired w/ bin+timeline selection (Keyline pattern) |
| 9 | InlineEdit | `inline_edit.h/.cc` | `SetText; SetOnCommit/OnCancel; click/double-click to edit; Enter=save Esc=revert; async-valid` | XS (1 d) | Rename clips/bins/markers in place |
| 10 | Stepper | `stepper.h/.cc` | `SetSteps(titles); SetActiveStep/GetActiveStep; SetLinear(bool); SetCanAdvance(cb); indicator row + content host; state: done/active/upcoming` | S (1–2 d) | Export/delivery wizard, project setup, import flow |
| 11 | BulkActionBar | `bulk_action_bar.h/.cc` | `BindSelection(provider); SetActions(buttons); float above list when selection>0; Esc clears` | XS (1 d) | Multi-asset bin ops, batch render ops |
| 12 | UnitInput / DurationInput | fold into `scrub_number_field` | `SetUnits({id,label,factor}); value stored in base unit; typed "2gb"/"90s" parses` | S (bundled w/ #1) | Bitrate, frame rate, memory/disk, duration fields |
| 13 | HistogramRange | `histogram_range.h/.cc` | `SetData(values); SetRange(lo,hi)/SetOnRangeChanged; buckets auto; in/out-of-range tint` | S (1–2 d) | Filter bin by duration/file-size distribution |
| 14 | SortableList | `sortable_list.h/.cc` | `SetItems; drag reorder + Alt+arrows; SetOnReorder(from,to)` | M (2–3 d) | FX chain order, track/layer order, queue order |
| 15 | XY Pad | `xy_pad.h/.cc` | `SetValue(x,y); SetRanges; SetOnValueChanged; drag 2-axis; optional crosshair/snap` | S | Pan/position pads, EQ freq×gain, color offsets |
| 16 | Gantt | `gantt.h/.cc` | `SetRows({id,label,bars:[{start,end,color}]}); SetMarkers; zoom hours→quarters; drag/resize bars; today line` | L (5+ d) | Render queue / resource scheduling — low urgency (NLE timeline already specialized) |
| 17 | Kanban | `lane_board.h/.cc` | `SetLanes; SetCards; drag between lanes; per-lane rules` | M–L | Shot status board — nice-to-have, not blocking |
| — | DescriptionList | `description_list.h/.cc` | `SetItems({label,value,copyable,editable})` | XS | Inspector facts (alternative: fold into #8) |
| — | Row/Col/Grid containers | `layout_containers.h/.cc` | `Row(gap,align)`, `Grid(cols,gap)` widgets wrapping flex.h | S | Internal ergonomics — not user-facing |

### Recommended order

Sprint A (highest NLE value): **ScrubNumberField (#1)** → **TreeView (#2)** →
**VirtualList (#3)** → **DataTable upgrade (#4)**. That closes G3/G4/G5.
Sprint B (bin UX): TagsInput, FilterBar, FileDropzone, InlineEdit,
BulkActionBar, Inspector. Sprint C (flows): Stepper (+wizard), UnitInput
(merge into #1), HistogramRange, SortableList. Later: XY Pad, Gantt, Kanban,
layout containers.

## Sources

- drift-ui.dev (+ /llms.txt, /llms-full.txt — full prop tables quoted above)
- kinetic.itsjay.in (ScrubNumberField API reference + scrub-number-math lib)
- ui.emstein.com (Table on TanStack Table+Virtual)
- gridcn.vercel.app (DataGrid v2.0, capability matrix vs AG Grid/MUI X)
- data-table.openstatus.dev (/llms.txt — block recipes, createTableSchema)
- keyline-ui.dev (/llms.txt + /docs/components/{data-table,inspector}.md)
- panelui.dev/docs/web (84-component index)
- ui.ilinxa.com (/llms.txt — 64-item catalog)
- kibo-ui.com (/components/gantt + nav index)
- layout.csikie.com (Layout Kit primitives)
- francozeta-stepper.vercel.app (headless stepper primitive)
- reui.io (/docs component index + blocks catalog)
