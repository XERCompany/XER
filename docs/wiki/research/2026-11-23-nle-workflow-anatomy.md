# NLE Workflow Anatomy — Source/Program, Media Browser, Transport

Date: 2026-11-23. Research pass over official product documentation for
Adobe Premiere (helpx.adobe.com), Blackmagic DaVinci Resolve 21 manual
(documents.blackmagicdesign.com content, chapter-mirrored at
imaster.org), Apple Final Cut Pro (support.apple.com/guide/final-cut-pro),
CapCut (capcut.com/help) and Descript (help.descript.com), gap-analyzed
against `tools/video_editor/ui2/` (the sole VE shell since the 2026-11-22
swap-over — `ui2/README.md`).

ui2 files audited for this pass:

- `monitor_panel.{h,cc}` — chrome around a `MonitorSurface`: transport
  row, per-scope edit cluster (Source: IN/OUT + INS/OVR; Program:
  IN/OUT + LIFT/EXT), safe/grid/loop toggles, quality select, meters.
- `monitor_surface.{h,cc}` — letterboxed frame + ruler (work-area
  brackets, loop band, marker flags, source marks), zoom/pan, safe
  margins/grid overlays.
- `preview_binding.{h,cc}` — per-monitor `PreviewEngine` + transport:
  `TogglePlay`, JKL `ShuttleStep` ladder ±{1,2,4,8} (`kMaxShuttleRate
  = 8.0`), `ShuttleStop`, `Seek`, `StepFrame`, `ToStart/ToEnd`, loop
  + quality commands; worker→UI marshal via `DrainPending`.
- `ve_app.{h,cc}` — shell: exclusive playback between the two
  bindings (shared audio endpoint), `OpenInSourceMonitor` (synthetic
  one-clip project + per-path `SourceMarks` persistence),
  `EditSourceToTimeline` (`,`/`.`), `ProgramLiftExtract` (`;`/`'`),
  `ProgramSetWorkAreaPoint` (program I/O writes the work area).
- `shortcut_router.{h,cc}` — JKL/I/O bound per scope
  (`kScopeProgram`/`kScopeSource`), `ActivateScope()` last-active-
  viewer routing wired to `MonitorHooks::on_activate`.
- `media_panel.{h,cc}` + `bin_surface.{h,cc}` — catalog bin: search,
  kind filter, 5-key sort, grid/list toggle, marquee select, context
  menu, poster LRU, drag-out handoff.
- `drag_session.{h,cc}` — bin→timeline drag: hover accent frame on
  `TimelineSurface`, drop resolves to (time, track).
- `timeline_surface.{h,cc}` + `timeline_strip.h` — tools
  V/C/B/N/Y/U/R/H/Z, snap toggle (S), LINK (linked selection), track
  headers (mute/eye/lock/solo/arm/expand), work-area bar, external-
  drop hover.

---

## 1. Source / Program monitor model

| aspect | Premiere | Resolve | FCP | CapCut | **ui2 today** | gap |
|---|---|---|---|---|---|---|
| viewer topology | Two persistent monitors side-by-side (Source, Program). Blue border marks the active one. | Dual viewer default (Source left, Timeline right); `Workspace > Single Viewer Mode` swaps to one contextual viewer. `Q` toggles viewer focus. | One main viewer; optional **Event Viewer** (Ctrl-Cmd-3) shows browser clips beside the timeline frame. Skimmer doubles as a "live source cursor" anywhere. | Single Player panel; no source monitor. | `MonitorPanel Kind::Program/Source` pair in a `Resizable` split; both always visible. `ActivateScope` picks which transport keys hit. | Single-viewer mode for small screens; explicit active-viewer accent border (see §5). |
| what loads into source | Double-click in Project panel, Media Browser, **or a Timeline clip** (match-frame use). Recents drop-down holds multiple open clips. | Double-click/Enter on Media Pool item; **Source Tape** mode shows the whole bin as one tape; a *timeline* can be loaded as source (blue playhead). `Live Media Preview`: hover-skimming a pool thumbnail scrubs it in the Source Viewer without opening. | No source monitor: browser filmstrip + skimmer. Event viewer optional. Timeline clips can't be "sourced" — reveal-in-browser instead. | n/a | `OpenInSourceMonitor` builds a synthetic 1-clip project; bin double-click only. Per-path marks persist (`source_marks_` map). | Match frame (F) from a timeline clip; recents list; source-tape equivalent is far off but Live-Media-Preview (hover→source viewer) is cheap given the binding exists. |
| marks → timeline | Source I/O + timeline I/O/playhead = three points; 4th inferred. `,` insert / `.` overwrite. Reverse three-point (timeline I/O + one source mark) supported. In/Out grips draggable on the ruler; "In/Out duration" readout. | Source In/Out + timeline In/Out/playhead; preview marks drawn during a 3-pt edit and draggable to retime the *other* side. Backtiming (source-out ↔ timeline-out) documented. Split (video/audio) in/out for L/J cuts. | I/O creates a **range selection** on the browser filmstrip (draggable range handles); edit lands at skimmer/playhead. Backtimed connect (Shift-Q). | None — drag card to timeline or `+` to append. | `SourceMark` I/O, `Alt+I/O` clears, marks persist per path and restore on reopen; `EditSourceToTimeline` resolves range via `ResolveSourceRange` (in/out or whole). `,`/`.` bound. Program I/O = work area. | Draggable mark grips on the source ruler; in/out duration readout; reverse-3pt & backtiming; split A/V marks; preview marks on target. |
| edit verbs | Insert (`,`), Overwrite (`.`), Lift (`;`), Extract (`'`), Replace, Apply Defaults; drag source→program monitor drops on edit-type overlays. | F9 insert, F10 overwrite, F11 replace, F12 place-on-top, Shift-F10 ripple-overwrite, Shift-F11 fit-to-fill, Shift-F12 append-to-end; drop on **Timeline Viewer overlays** picks the verb; drag to timeline = overwrite/insert by modifier. | Q connect, W insert, E append, D overwrite (magnetic model — no track patching; vertical position picks lane). | `+` on card = append at playhead/end; drag for position. | Insert/overwrite/lift/extract only; drop goes straight to `TimelineSurface` (time+track), no edit-type overlays; no replace/fit-to-fill/append/ripple-overwrite/place-on-top. | The edit-verb vocabulary is ~40% of Premiere/Resolve's. |
| track patching | **Source patching** column: per source stream V1/A1.. patch icons drag next to target tracks; patching ≠ targeting. Disabled patch = stream not edited in. | **Source Track Selector**: blue source icons (v1, a1..n) left, red destination controls (V1, A1..) right; drag to patch; Opt-1..8 sets video dest, Cmd-Opt-1..8 audio; click toggles a stream off; per-audio-channel source pick via context menu. | None — no tracks; connected clips attach to primary storyline positionally. | None — auto track pick. | No patching/targeting UI at all. `EditSourceToTimeline` picks the selected clip's same-kind track else first free kind-matched unlocked track; A/V sources auto-create a linked pair. | **Biggest structural gap**: header patching column + honoring it in the source-edit path. |
| three-point rules | Any 3 of src-in, src-out, tl-in, tl-out; playhead is implicit tl-in when no marks; reverse 3pt fills a marked tl range. | Same rules + documented edge cases (four-point = fit-to-fill or last-wins; fewer than 3 = clip-duration defaults). Preview marks visualized *before* the edit lands. | Three-point supported formally (browser range + playhead + one endpoint). | n/a | Insert/overwrite land at playhead with source range only — tl in/out (work area) is ignored by `EditSourceToTimeline`. | Honor work-area/timeline marks as the 3rd/4th point; add preview mark visualization. |

## 2. Transport

| aspect | Premiere | Resolve | FCP | CapCut | **ui2 today** | gap |
|---|---|---|---|---|---|---|
| JKL ladder | Space/L play, J reverse, K stop; repeat J/L steps speed up; Shift+J/L slow shuttle (half speed each direction). | J=100% back, K=stop, L=100% fwd; repeat presses increase speed (documented up to ~64x on some pages); **speed indicator** appears in viewer. K+J or K+L held = slow-motion shuttle (pitch-corrected audio on macOS). | J/K/L up to 32x; Option-J/L = half speed; playback starts from playhead (project) or **skimmer** (clip). | Basic space/scrub-bar play; some JKL support. | `ShuttleStep` ±{1,2,4,8}x, `kMaxShuttleRate = 8.0`, opposite key halves & flips at 1x; K = `ShuttleStop`. | Max rate below others (8x vs 16–64x); no Shift+J/L slow rung; no speed indicator chrome. |
| frame step | ←/→; Shift+←/→ = 5 frames; **K+J / K+L while held** = step 1 frame. | K held + tap J/L = ±1 frame; ←/→ also step; Shift+←/→ = 1 second. | ←/→ step 1; Shift+←/→ step 10; hold K + J/L steps. | Frame-step buttons. | Arrows bound per scope (`ProgramStepFrame`/`SourceStepFrame`); no held-modifier combos — the router consumes `KeyDown`/`TextInput`, never `KeyUp`, so K-held states can't exist today. | Router needs key-up/held tracking (or chord-on-release semantics) for K+J/K+L. |
| play-around / play-selection | Shift+K **Play Around** (pre/post-roll around playhead); Opt+K Play In to Out. | `/` Play Around current selection/clip/frame; Opt+Space around In; Shift+Space around Out; Opt+`/` Play In to Out; configurable pre/post-roll. | Shift+? Play Around; `/` Play Selection. | n/a | Loop toggle + loop range exists (`SetLoopEnabled/SetLoopRange`) but no one-shot play-around command. | Bind Shift+K / `/`: play pre→post around playhead once (no loop). |
| go-to marks / edits | Shift+I/O go to in/out; ↑/↓ prev/next edit point; Home/End; M mark; Shift+M next marker. | Shift+I/O go to in/out; ↑/↓ prev/next clip-or-edit; Home/End; `;`/`'` first/last frame. | Shift+I/O range start/end; ↑/↓ next edit. | n/a | Home/End per scope; PageUp/Dn + Shift+M jump markers; M adds marker. | Shift+I/O and ↑/↓ edit-point nav are the cheap missing keys. |
| loop / resume | Loop playback toggle; Play In to Out respects loop. | Cmd+`/` loop; **Stop and Go to Last Position** (Opt+K) — playhead returns to where playback began; audio scrubbing toggle. | Cmd+L loop; continuous browser playback option. | Loop toggle on player. | Loop flag + range plumbed to engine (`ApplyLoopCommand`); no stop-returns-to-start mode; no audio-scrub switch. | Two small engine/policy flags. |
| source↔program sync | **Gang Source and Program Monitor** — playheads lock together for shot comparison. | Separate playheads; `Q` flips focus; no gang. | Skimmer is inherently position-coupled to viewer. | n/a | Hard **exclusive** playback (sibling stops — shared audio endpoint). | Gang toggle: shared position callback between the two `PreviewBinding`s. |
| exclusive playback | Both monitors can technically play; active one wins audio. | Same. | n/a | n/a | Enforced in shell (`ProgramTogglePlay` calls `StopSourceTransport`). | Already correct. |

## 3. Media Browser vs Project Bin

| aspect | Premiere | Resolve | FCP | CapCut | **ui2 today** | gap |
|---|---|---|---|---|---|---|
| two kinds of browsing | **Project panel** = imported assets; **Media Browser** = filesystem navigator w/ favorites, type filter, thumbnail preview, ingest-on-import. Separate Import-mode screen on new projects. | **Media Storage** (Media page) = filesystem; **Media Pool** = project assets, same panel on every page. | Browser = library/event contents only; Finder for filesystem. Import dialog bridges. | Media tab = imported + stock libraries; no filesystem browse beyond the import picker. | `MediaPanel` = catalog only (`SetEntries` from `MediaCatalog`); file import via File menu/Import button → OS picker. | No filesystem browsing panel; Premiere's split (bin vs media browser) is the norm. |
| views | List / Icon / **Freeform** (spatial storyboard layouts, saved per project). | Thumbnail / List (customizable metadata columns) / **Metadata view** (card w/ scrubbable thumb + fields, auto-clusters by sort). Per-bin view memory ("Always open as"). | Filmstrip / List (sortable columns; selected clip's filmstrip docks at top of list). | Grid only. | Grid (`BinGridSurface`) / list (`VirtualList`) toggle. | List has no column model; no filmstrip-strip for the selected row; no freeform. |
| hover scrub | **Hover Scrub** in Icon/Freeform view — cursor X maps to media position, marks I/O without opening (Shift+H toggles). | Live Media Preview: thumbnail skims → Source Viewer follows; Media-page thumbnails hover-scrub directly. | **Skimming is the core interaction** — filmstrip & list-view filmstrip skim continuously, audio skimming optional (Shift-S). | Hover shows a preview frame/title (no scrub contract documented). | `hovered_` index only drives a highlight tint — poster is static. | Wire hover X → `get_poster(path, t)`; the tri-state hook already takes seconds. |
| in/out in the bin | Hover-scrub + I/O keys mark the clip (icon view); marks show on the filmstrip strip in List view preview area. | I/O can be set **directly on the Media Pool filmstrip** (List view) — heads/tails dim; marks persist per clip; duration-marker convert. | Drag on filmstrip = range selection (the I/O analog); ranges rate-able/keyword-able individually. | n/a | Nothing — I/O only exists once a clip is in the source monitor (`source_marks_`). | Mark-on-card + reuse `SourceMarks` per path; feeds EditSourceToTimeline without opening the viewer. |
| organization | Bins (nested, own windows), search bins, labels (colors), huge metadata column set. | Bins + **Power Bins** (cross-project) + **Smart Bins** (multi-rule metadata filters, folders of them); bin tabs; open-bin-as-window; flags; custom sort + snap-to-grid. | Libraries > events > keyword collections; **Smart Collections** (weighted multi-criteria filter window); roles. | Flat folders + stock tabs. | Flat catalog; substring search over path; kind filter; 5 sort keys. | No folders/bins at all — the biggest bin gap before smart-anything. |
| culling metadata | Labels; "used in sequence" filmstrip badge shows count + jump-to-use. | Flags (colors), ratings via metadata; "used" red-dot; custom metadata columns editable inline. | Favorite/Reject (F/Del/U) with green/red top lines; **Used Media Ranges** orange underline. | n/a | Proxy badge only. | Ratings/labels/used-indicator absent — needs catalog schema fields. |
| search | Field search + media-intelligence (visual/audio) search panel. | Text search + **IntelliSearch** (objects, faces, dialog) + "Add as SmartBin". | Text + Transcript Search + Visual Search + weighted Filter window. | Stock search. | Substring on path only. | Filter-on-metadata (duration, codec, flags) is the parity step; AI search exists in our stack already (ai_tags). |

## 4. Timeline conventions

| aspect | Premiere | Resolve | FCP | CapCut | **ui2 today** | gap |
|---|---|---|---|---|---|---|
| patch/target column | Source patching (per-stream, draggable) + **track targeting** (V1/A1.. toggles govern paste/lift/extract); sync lock icons; lock. | Destination controls (red) + **Auto Select** controls (gate range ops/delete scope; Opt-F1..8, solo-able) + Sync Lock + Lock + disable. | No tracks — lanes + roles color; skimmer position + storyline selection govern targeting. | Track header: lock/hide/mute; no targeting. | Headers: mute/eye/lock/solo/arm + expand chevron; `TrackAtDropPoint` for drops. | No destination/auto-select column → range ops (lift/extract) apply to ALL tracks unconditionally today. |
| linked selection | Linked-selection toggle + Alt-click temporary unlink. | Cmd-Shift-L linked selection + "Linked Move Across Tracks" pref. | Connections are structural (no toggle needed). | Link icon on A/V pairs. | `LINK` strip button → `SetLinkEditsEnabled` → link_group snapshot in Selection tool. | Parity. |
| snapping | Snap toggle (S); snap targets = edges/playhead/markers; dragged edge visibly **jumps** + guideline. | `N` toggles; snap to points/markers/edits/playhead; snapped position highlighted. | Skimmer/playhead turn **yellow** when snapped; snapping near-instant feel. | Snap toggle + edge hints. | `snap_enabled_` + `SnapCandidate` math; **no visual snap indicator** drawn during drags. | Draw the snap guide line when a drag lands on a candidate (S-effort paint addition in `timeline_surface.cc`). |
| trim UI | **Trim mode**: Program Monitor 2-up (outgoing left, incoming right), trim-type indicator, out/in shift counters, ±1/±5 buttons, default-transition button, loop-around-edit playback, JKL dynamic trim, numeric-entry trim. | Trimming an edit shows a **Trim Viewer** two-up with outgoing/incoming + trim-side highlight; `W` = dynamic trim mode (JKL trims live); `T` trim tool, `A` selection tool; slide/ripple overlays on the timeline. | Precision editor (inline double-roller filmstrip on the edit point). | Drag edges; preview shows the edge frame. | Tools B (ripple) / N (rolling) / Y (slip) / U (slide) operate on the surface; program monitor shows only the playhead frame. | No two-up trim presentation — the single most visible "pro" monitor feature ui2 lacks. |
| drop semantics | Drag to timeline = insert/overwrite by Ctrl modifier; drag to **program monitor** = overlay grid picking insert/overwrite/replace/etc. | Drag clip onto Timeline Viewer → full overlay palette (insert/overwrite/replace/place-on-top/fit-to-fill/ripple-overwrite/append). | Drop picks connect/insert/overwrite by drop zone. | Drop inserts at position. | Bin drag → `BinDragSession` → accent frame → drop = overwrite-style insert at (time,track). | No verb overlays on the program monitor; no insert-vs-overwrite modifier. |
| markers/marks parity | Sequence markers + clip markers distinct; color-coded. | Timeline + clip markers; duration markers convertible to/from I/O. | Markers + to-do markers; keywords on ranges. | Basic markers. | Marker lane: dbl-click add, context remove; `SetMarkers` mirrored on monitor rulers. | Duration markers / range markers missing (model-level). |

## 5. UI framework anatomy

| aspect | Premiere | Resolve | FCP | CapCut | **ui2 today** | gap |
|---|---|---|---|---|---|---|
| dock model | Free docking: panels dock/group/undock anywhere + tab stacking; **workspaces** = named layouts (Editing/Color/Audio…, custom, reset); `~` maximizes panel. | Fixed **pages** (Media/Cut/Edit/Fusion/Color/Fairlight/Deliver) with per-page panel presets + dual-screen layouts + workspace presets. | Fixed regions (browser/viewer/inspector/timeline) + a few workspace presets (Default/Organize/Color & Effects); event viewer toggles a column. | Fixed 4-panel layout (Media / Player / Inspector / Timeline). | `Resizable` splits + left/right `DockTabGroup`s + `WorkspaceLayout` presets + `ShellPrefs` persistence + nav rail. | Functionally between Premiere and Resolve — no arbitrary re-docking (tabs are fixed per dock). Acceptable; Premiere-grade docking is L and low-value. |
| focus indicator | **Blue border** around the active panel/monitor — drives which keys bind. | Active viewer gets a subtle highlight; `Q` toggles viewers. | Selected area highlight (yellow border on browser range). | n/a | `ActivateScope` routes keys but nothing in `MonitorPanel` draws "I own J/K/L" — the user can't see which viewer is armed. | Accent border/title tint on the scope-active monitor — tiny change, removes a whole class of confusion. |
| per-panel menus | **Hamburger menu on every panel** (view options, panel settings, undock, close). | **3-dot option menu** per panel (e.g., Source Viewer: Live Media Preview, display modes). | Pop-up menus per region (Clip Appearance, Filter). | Tab-local menus. | `DockTabGroup` has no option-menu affordance; panel-local settings live in shell menus or nowhere (hover-scrub toggle, loop prefs, monitor settings). | Add a `⋯` slot per dock tab → per-panel menu seam (media panel: view/search-bar toggles; monitors: gang/loop/quality/scrub prefs). |
| transport chrome | Button editor per monitor (customizable transport row); timecode + duration + zoom + playback-resolution selects; in/out grip marks on ruler. | Transport row + **jog bar** (full-width = whole clip/timeline) + separate slow-jog dial control; source vs timeline timecode readouts. | Minimal: play, timecode, skimmer. | Player buttons + scrub bar. | Buttons \|◄ ‹ ►/❚❚ ■ › ►\|, timecode, quality select, zoom slider, ruler = jog bar equivalent. | Dedicated jog/shuttle wheel control is the remaining hardware-style affordance (low priority). |

---

## Prioritized gap list for ui2

Ordered by user-visible parity value. Effort: **S** ≈ <300 LOC in
existing seams, **M** ≈ 1–3 files + a smoke check, **L** ≈ new
subsystem/model change.

| # | gap | effort | owning file(s) | notes |
|---|---|---|---|---|
| 1 | **Active-viewer accent border** (Premiere blue frame) | S | `monitor_panel.cc` (paint bound state), `ve_app.cc` (scope owner push) | Router already tracks `ActiveScope()`; panel just needs a `SetScopeActive(bool)`. Kills "which monitor owns J/K/L" confusion. |
| 2 | **Shift+I/O go-to-mark, ↑/↓ prev/next edit, Shift+K play-around** | S | `ve_app.cc` bindings, `preview_binding.cc` (play-around = bounded play once) | All documented in §2; pure key+seek work. |
| 3 | **Snap indicator line** during clip drags | S | `timeline_surface.cc` (Render: draw guide at `SnapCandidate` result) | Resolve/Premiere both show the snapped point; today snapping is invisible. |
| 4 | **Shift+J/L slow shuttle + speed badge** | S | `preview_binding.{h,cc}` (sub-1x rung + expose rate), `monitor_panel.cc` (rate chip) | ±{0.25,0.5} rungs below 1x; badge reads "4x". |
| 5 | **Hover-scrub bin cards** | M | `bin_surface.cc` (hover X → normalized seconds → `get_poster`), `media_panel.cc` | Hook signature already takes `seconds`. Gate behind a pref (Premiere Shift+H parity). |
| 6 | **Match frame / open timeline clip in source** | S | `ve_app.cc` (`OpenInSourceMonitor` reuse + source-time offset from `Clip::start`), bind `F` | Premiere's `F`; cheap since marks/synthetic-project path exists. |
| 7 | **Source patching + track targeting** | M→L | `timeline_surface.{h,cc}` (header patch column), `ve_app.cc::EditSourceToTimeline` (honor patch, enable/disable streams), `timeline_model.h` (persist target on `Project` or shell-side set) | **Highest-value structural gap.** Decide whether patching lives on the model (Premiere semantics, serializable) or shell state (Resolve destination controls are session state). Must also gate lift/extract scope by target/auto-select. |
| 8 | **I/O marking on bin cards** (filmstrip marks) | M | `bin_surface.cc`, `ve_app.cc` (`source_marks_` reuse) | With #5, I/O on the skimmed card = mark without opening the source viewer (Resolve filmstrip parity). |
| 9 | **Play In-to-Out + stop-returns-to-start + audio-scrub flag** | S | `preview_binding.cc`, `monitor_panel` prefs | Three documented toggles; loop plumbing already exists. |
| 10 | **Edit-verb completeness**: replace, append-to-end, place-on-top, fit-to-fill, ripple-overwrite | M | `ve_app.cc` (extend `EditSourceToTimeline` family), `timeline_bridge.cc`/history (ripple-overwrite atomicity), `monitor_panel.cc` (button cluster) | Follows Resolve's F9–F12+Shift table verbatim; the mark-resolution code generalizes. |
| 11 | **Program-monitor drop overlays** (insert/overwrite/replace zones) | M | `monitor_surface.cc` (overlay hit zones during drag), `drag_session.cc` (target = surface or monitor) | Resolve's overlay palette; extends `BinDragSession` beyond the timeline target. |
| 12 | **K-held combos** (K+J/K+L frame-step, K+J/L slow shuttle) | M | `shortcut_router.{h,cc}` (KeyUp/held tracking), kit `Event` channel | Contract change: router currently never sees KeyUp. Also enables Premiere's K+L-while-held trim stepping later. |
| 13 | **Recents list + in/out duration readout in source monitor** | S | `monitor_panel.{h,cc}` (title drop-down + duration chip), `ve_app.cc` (recent-path deque) | Premiere's source-menu parity; `source_marks_` map is already the store. |
| 14 | **Trim-mode monitor** (2-up outgoing/incoming + shift counters + JKL dynamic trim + loop-around) | L | `monitor_panel`/`monitor_surface` (2-up mode), `preview_binding` (two-frame feed at edit point), `ve_app.cc` + timeline hooks (edit-point selection model) | The Premiere/Resolve differentiator; needs an edit-point-selection concept the surface doesn't expose yet. Plan as M5 wave with #12 (JKL dynamic trim rides the same held-key work). |
| 15 | **Bin structure**: folders/bins tree + per-bin view memory | L | `media_catalog` (schema: bin_id/folder), `media_panel.cc` (tree sidebar), `shell_prefs` or catalog (view-per-bin) | Required before smart bins/ratings mean anything; blocks #16. |
| 16 | **Labels/ratings/keywords + used-indicator + smart bins** | L | `media_catalog` (columns: label, rating, keywords[]; used-in-project refs), `bin_surface`/`media_panel` (chrome + filter rules), `timeline_bridge` (usage count) | FCP favorite/reject + Resolve smart bins; the catalog already has per-path rows — additive columns. |
| 17 | **Filesystem media browser** (separate from the bin) | M | new `media_browser_panel` + `MediaPanel` tab or second left-dock tab | Premiere Media Browser / Resolve Media Storage parity; can reuse `BinGridSurface` with a directory model behind it. |
| 18 | **Gang source↔program** | M | `preview_binding` (position callback between bindings), monitor option menu (#19) | Premiere's Gang; conflicts with exclusive playback only in audio ownership — gang = position lock, not dual play. |
| 19 | **Per-panel option (`⋯`) menus** | M | `shell_widgets.cc::DockTabGroup` (menu slot), each panel's toggles | Houses hover-scrub, gang, live-preview, loop prefs — replaces growing shell-menu surface area. |
| 20 | **Duration/range markers** | M | `timeline_model.h` (marker span), `timeline_surface.cc` + `monitor_surface.cc` (render), `ve_app.cc` (convert I/O↔marker) | Resolve's duration-marker workflow (log multiple takes per clip). |

Suggested landing waves: **wave A** = #1–#6, #9, #13 (one session
of pure seam work, all smoke-testable); **wave B** = #7 + #10–#12
(the editing-grammar wave: patching + verbs + held-keys); **wave C**
= #14 (trim monitor) after #12; **wave D** = #15–#17 (bin
infrastructure) whenever the catalog schema wave is scheduled.

### Implementation status (2026-11-25, base `d47f023`)

- **#7 patching/targeting — DONE (shell-state model).** Targeting
  lives on `TimelineSurface` (`SetTrackTargeted`/`TrackTargeted`/
  `FirstTargetedTrack`/`TargetedMask`/`ResetTrackTargets`), seeded
  to the first track per kind on `SetProject` (Premiere's default
  V1/A1 patch) and re-seeded per document in `VeApp::SetProject` —
  Resolve-style session state, never serialized. A V/A badge in the
  track header's upper band (hit-test/paint share one rect; header
  button id 6) toggles membership; untargeted badges dim. With
  `on_track_target` bound the badge reports the REQUESTED state and
  the shell stays source of truth; unbound, the surface owns the
  set. `EditSourceToTimeline` resolves video leg → first unlocked
  targeted V track, audio leg → first unlocked targeted A track
  (patched-off leg drops silently; a kind with lanes but nothing
  targeted errors instead of guessing). `ProgramLiftExtract` passes
  the mask into `TimelineHistory::LiftRange`'s new `track_target`
  param — untargeted lanes behave like locked ones.
- **#10 verbs — PARTIAL.** Landed: replace
  (`VeApp::SourceReplaceEdit` — three-point swap keeping the
  timeline clip's footprint/id/effects/link_group, clamps a short
  source to media bounds), fit-to-fill (`SourceFitToFill` —
  rate = source span / work-area-or-targeted-clip span, one history
  macro via `OverwriteClipAt`), top-and-tail (`ProgramTopTail` —
  Q/W-style head/tail ripple trim to the playhead via
  `RippleTrimClip`), timeline context-menu verb dispatch
  (`RunSourceVerb` over insert/overwrite/replace/fit-to-fill at a
  context point via `SourceEditVerb` + `on_source_verb` hook), and
  insert/overwrite honoring targets + `at_override`. Still open:
  append-to-end, place-on-top, ripple-overwrite, monitor drop
  overlays (#11), and the Resolve F9–F12 keys (the kit `KeyCode`
  table has no F9–F12, so bindings currently ride letter keys).
- **#14 trim monitor — PARTIAL (timeline-side overlay).** Edge-grab
  arms a trim-preview state on the surface (`ArmTrimPreview`/
  `RefreshTrimPreview`/`DisarmTrimPreview`): roll drags resolve the
  A/B pair's source frames from `clip_previews_`, single-edge trims
  show the moving side + still neighbor (empty pane across a gap),
  and a ±frames counter rides the overlay (`TrimOffsetSeconds`/
  `TrimOffsetFrames`). Release or Esc disarms. The program-monitor
  2-up presentation, JKL dynamic trim, and loop-around playback are
  still open (monitor surface work was scoped to another agent).
- **Paste-against-targets: N/A** — there is no clipboard/paste path
  in the codebase yet; Premiere's "paste respects targeted tracks"
  has nothing to land on.
- Coverage: `ui2_timeline` smoke gained `CheckTrackTargeting`
  (seeded defaults, badge-click toggle, hook-bound request path,
  mask) and `CheckTrimPreview` (arm/counter/A-B sides/Esc); the
  `ui2_monitor` smoke gained `CheckEditVerbs` (targeted overwrite /
  insert-at-point / masked lift+extract / replace / fit-to-fill /
  top-and-tail round-trips through history Undo).

---

## Appendix — source URLs (accessed 2026-11-23)

Premiere (helpx.adobe.com):

- `premiere/desktop/get-started/source-and-program-monitor-adjustments/about-source-monitor-and-program-monitor.html`
- `premiere/desktop/get-started/source-and-program-monitor-adjustments/set-in-and-out-points-in-the-source-monitor.html`
- `ee/premiere-pro/how-to/three-point-edits.html`
- `au/premiere-pro/using/source-patching-track-targeting.html`
- `au/premiere-pro/using/playing-assets.html` (JKL table, Play Around,
  K+J/K+L frame-step, blue-border active monitor)
- `premiere/desktop/edit-projects/trim-clips/about-trim-mode.html` +
  `au/premiere-pro/using/trim-mode-editing.html` (2-up UI, shift
  counters, JKL dynamic trim, loop-around playback)
- `premiere/desktop/get-started/customize-the-project-panel/*`
  (List/Icon/Freeform views, hover scrub, Shift+P poster frame)
- `premiere/desktop/organize-media/file-organization/bins-overview.html`,
  `ee/premiere-pro/how-to/import-with-media-browser.html` (Media
  Browser = filesystem panel)
- `premiere/desktop/get-started/keyboard-shortcuts/default-keyboard-shortcuts.html`

DaVinci Resolve 21 manual (Blackmagic; chapter text via imaster.org
mirror of documents.blackmagicdesign.com content):

- Part 6 / Ch. 40 (Edit page layout), Ch. 42 (Source/Timeline viewers,
  Single Viewer mode, Live Media Preview, JKL — incl. K-held slow
  shuttle + frame taps, play-around family, loop, stop-and-go-to-last,
  source-clip markers, in/out persistence, duration markers, subclips)
- Ch. 43 (edit basics: N snap toggle, Cmd-Shift-L linked selection,
  destination-control + auto-select shortcuts)
- Ch. 46 (Source Track Selector blue/red patching, three/four-point
  rules, preview marks, F9/F10/F11/F12 + Shift edit verbs, mark clip X
  / mark selection Shift-A)
- Ch. 51 (trim: T/A tools, W dynamic trim, Trim Viewer two-up)
- Part 3 / Ch. 17 (Media Pool: thumbnail/list/metadata views,
  hover-scrub, Power Bins, Smart Bins, flags, bin tabs/windows,
  custom sort, IntelliSearch)
- blackmagicdesign.com/products/davinciresolve/cut (Source Tape, dual
  timeline)

Final Cut Pro (support.apple.com/guide/final-cut-pro):

- `skim-media-vere9ba3609`, `intro-to-playback-veradb8241c`,
  `play-media-ver90ba4ef0` (skimmer vs playhead, JKL→32x,
  Option-J/L half speed, Play Around Shift+?, `/` Play Selection,
  Cmd+L loop, continuous browser playback)
- `play-media-in-the-event-viewer-verd64b1523` (event viewer)
- `intro-to-browser-views-verd00d3ae7` (filmstrip/list),
  `rate-clips-ver30ccd91f`, `find-clips-and-projects-ver65764b45`,
  `sort-and-group-items-in-the-browser-ver53cadfb73`
- `append-clips-…ver7a77f584`, `insert-clips-ver4e2eff6`,
  `connect-clips-ver7a77ef9e` + `keyboard-shortcuts-ver90ba5929`
  (Q/W/E/D edit verbs)

CapCut: capcut.com/help import/asset pages (media-tab model; no
source-monitor or marks contract — single player + drag/append).

Descript (help.descript.com): `descript-tour/the-editor-interface`,
`project-panel`, `media-panel` (script-first model; project files +
shared drive media library; no source monitor — transcript selection
is the marking surface).
