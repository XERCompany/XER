# Bug Audit — Video Editor UI + App Runtime (Part 1 of 2)

Date: 2026-11-20 · Scope: `tools/video_editor/ui/*`, `tools/video_editor/app_runtime/*` · Not covered: media_prep, compositor, export engine, playback, plugins host internals (sibling agent).

Severities: **CRASH** memory safety / abort · **DATA-LOSS** silently destroys user data · **LOGIC** wrong behavior · **LEAK** unbounded growth · **STUB** dead/incomplete path.

---

## CRASH

1. **CRASH** `ui/browser_panel_widget.cc:87` — `events.on_loading` lambda captures `this` and writes `status_` directly from the CEF callback thread with no mutex, while `Render()` (line ~206) reads it on the UI thread; there is also no lifetime barrier, so a callback in flight after `~BrowserPanelWidget`/`cef_` teardown dereferences freed memory.
   *Fix:* route loading state through `g_event_mutex`/pending queue like URL/title, and clear callbacks before `Close()`.

2. **CRASH** `ui/video_editor_app.cc:2774` — `RunLabOperation` runs on the lab worker thread but reads `project_` (`compositor.Init`), `playhead_` (`RenderFrameAt`), and `project_path_` with no synchronization while the UI thread mutates them via `history_` ops → torn reads / iterator invalidation inside `Project`.
   *Fix:* snapshot `project_`/`playhead_`/`project_path_` under a mutex before `op_.Start`, and pass copies into the worker.

3. **CRASH** `ui/timeline_widget.cc:483` — `WaveFor` indexes `peaks->data[ch*buckets*2 + b*2]` trusting the `AudioPeaks` contract (`channels*buckets*2` floats) but `PeaksFor` (line ~407) only validates `buckets>0 && channels>0`, never `data.size()` — a corrupt/partial catalog payload gives an OOB read. Note `waveform_panel.cc:119` performs exactly this check; timeline does not.
   *Fix:* validate `data.size() >= channels*buckets*2` in `PeaksFor` before caching.

4. **CRASH** `ui/nav_rail_widget.cc:151` — `Click` indexes `top_[pressed_]` / `bottom_[DecodeBottom(pressed_)]` with no bounds check; `SetItems` (line ~73) replaces the vectors without resetting `pressed_`/`hovered_`, so a press started before an item-list update resolves an out-of-range index → OOB.
   *Fix:* reset `pressed_`/`hovered_` in `SetItems`, and bounds-check decoded indices.

5. **CRASH** `ui/media_bin_widget.cc:85` — `OiioThumb` returns `const Io::Image&` into a static `cache`; when the cache hits 64 entries it calls `cache.clear()`, invalidating every previously returned reference — any caller holding a ref across a second `OiioThumb` call reads freed memory.
   *Fix:* return `shared_ptr<const Io::Image>`, or evict one entry instead of `clear()`.

6. **CRASH** `app_runtime/app_runtime.cc:348` — `SharedBootEngine() = result.engine.get()` stores a raw global pointer to the `JsEngine` owned by `BootResult.engine`; nothing resets it on `BootResult` destruction (main.cc:107 keeps `boot` alive only for `main`'s scope) → dangling global → UAF in `FirePluginLoad`/plugin host.
   *Fix:* make `SharedBootEngine` a `weak_ptr`, or have the boot owner explicitly clear it at shutdown.

7. **CRASH** `app_runtime/js_engine.cc:507` — `CallHandler` does `impl_->handles[it->second.handler_id].As<v8::Function>()` — an unchecked `As<Function>` on a `v8::Local` that may hold a non-function object (handler ids and object ids share the `handles` array; `export_appdata.cc` `ObjectRef` stashes object ids in the same space) → V8 CHECK abort.
   *Fix:* `Local<Function>::Cast` guarded by `->IsFunction()`, reject non-functions.

8. **CRASH** `ui/monitor_widget.cc:348` — `SetExternalFrame` copies `bgra` into `frame_` and passes `frame_.data()` to `ApplyMonitorDisplayTransform` and `scopes.widget.SetFrame` with `stride = width*4`, but never checks `bgra.size() >= width*height*4` — a malformed engine frame gives OOB reads/writes across the transform and scope scan.
   *Fix:* reject frames whose byte size < `w*h*4`.

9. **CRASH** `ui/markers_panel.cc:96-118` (+ `inspector_widget.cc:~414`, `export_queue_panel.cc:129-160`) — row remove/jump/cancel buttons trigger hooks → `NotifyProjectChanged` → `SetProject`/`SetJobs` → `RebuildRows` → `RemoveChild(row.remove)` + `rows_.clear()` **destroys the very button whose `OnClick` is executing**; if the tree's dispatch doesn't hold a `shared_ptr` to the child, `button.cc:295` resumes on a freed `this`.
   *Fix:* defer child destruction (mark-and-sweep) or hold a strong ref during event dispatch.

---

## DATA-LOSS

10. **DATA-LOSS** `ui/keyframe_editor.cc:194` — `SortDedupeKeys` erases near-duplicate-time keys via `std::unique`, which keeps the **first** element of each equal run — but the comment says "keep the later (dragged-to) value," and `std::sort` is unstable so equal-time order is arbitrary → the value the user just dragged can be silently discarded.
    *Fix:* after sorting, walk runs of equal-time keys and keep the last (or the dragged) element, e.g. `std::stable_sort` + `unique`-on-reversed-order or explicit last-wins merge.

11. **DATA-LOSS** `ui/inspector_widget.cc:439` — `RebuildFxRows` unconditionally `fx_disabled_.clear()`s; `RebuildDynamicRows` calls it whenever mask/effect counts change, so a user who unchecks an effect row and then triggers any rebuild (add mask, apply another field) has the staged exclusion silently dropped → the effect is re-applied on next `Apply()` despite the unchecked box.
    *Fix:* only clear `fx_disabled_` when the effect list itself was replaced, or re-derive it from surviving indices.

---

## LOGIC

12. **LOGIC** `app_runtime/ui_manifest.cc:394` & `app_runtime/editor_config.cc:93` — both clamp `right_tab`/`RightTab` to `0..7`, but `video_editor_app.cc:3214` defines **ten** right-panel tabs (0–9 incl. Color=8, Browser=9) → workspace/editor config can never restore tabs 8/9; `workspace_layout.h` docs still say 0..5.
    *Fix:* clamp to `0..9` (or to the tab count constant) in both parsers.

13. **LOGIC** `ui/video_editor_app.cc:223` — `EnsureChildren` applies `layout_cfg.right_tab` only when `> 0`, so a configured `right_tab = 0` (Inspector) is ignored and the default wins; `-1` is documented as "keep," making `0` unreachable.
    *Fix:* apply when `>= 0`.

14. **LOGIC** `ui/timeline_widget.cc:425` — `MediaDurationFor` caches `duration = 0.0` when `get_media_bounds` fails (`!bounds_ok`), and `media_duration_cache_` is never invalidated — once media arrives, the waveform permanently uses the `buckets*spp/48kHz` fallback span → misaligned waveform for the session.
    *Fix:* don't cache failures; retry on next paint, or invalidate on `ConsumeRenderDirty`.

15. **LOGIC** `app_runtime/svg_raster.cc:264` — `Attr()` matches `name + "=" + quote` as a raw substring: `d=` matches inside `id=`, `x=` inside `rx=`/`cx=`, `y=` inside `ry=`/`cy=` → `<path id="x" d="M..">` returns the **id** as path data (silently dropped icon), `<rect rx="4" x="10">` can return `rx` for `x` → wrong geometry.
    *Fix:* require a non-identifier char (space, `<`, `/`) immediately before the attr name, or parse attributes properly.

16. **LOGIC** `app_runtime/ui_manifest.cc:828` — `panels.yaml`: `min_size` (pixels, int) is compared against `panel.size` (a float that may be a 0..1 *fraction*): `static_cast<int>(panel.size)` yields `0` for fraction-sized panels → `min_size` silently clamps to 0 → panel can shrink to zero height.
    *Fix:* compare in the same units (resolve fraction→px first, or skip the clamp for fraction panels).

17. **LOGIC** `app_runtime/native_modules.cc:125` — `ExtractJsonStringField` ends the value at the next raw `"`, ignoring `\"` escapes → truncated values for any JSON string containing escaped quotes (concat/options payloads).
    *Fix:* scan with an escape-aware loop (as `StoreOutData`'s `ParseString` does).

18. **LOGIC** `app_runtime/app_runtime.cc:71` — `JsonEscape` only escapes `\` and `"`; control chars (0x00–0x1F incl. `\n`, `\r`, `\t`) pass through → `FirePluginLoad` emits malformed JSON for plugin paths/metadata containing them → `CallHandler` arg parse fails silently.
    *Fix:* share the fuller `JsonEscapeInto` from `native_modules.cc`.

19. **LOGIC** `ui/markers_panel.cc:106-117` — markers are identified to the hooks by `time` (`on_remove_marker(time)`, `on_rename_marker(marker.time, ...)`) — two markers at the same timestamp make remove/rename hit the wrong (first-matching) marker.
    *Fix:* pass a stable marker id/index; if the model only keys on time, dedupe on insert.

20. **LOGIC** `ui/inspector_widget.cc:805` — `ParseEffects` applies `fx_disabled_` indices (staged against `clip_.effects` at row-build time) to the freshly parsed `effects` list — if the user also edited the effects text field (reorder/delete), indices point at different effects → the wrong one is dropped.
    *Fix:* stage exclusions by effect identity, or disable the checkbox once text diverges.

21. **LOGIC** `ui/keyframe_editor.cc:348` — after `keys.erase(keys.begin() + hit)`, `selected_key_`/`drag_key_` are only cleared when `== hit`; a selected index `> hit` now points at the wrong key → next delete/drag acts on the wrong keyframe.
    *Fix:* decrement `selected_key_`/`drag_key_` when `> hit`.

22. **LOGIC** `ui/speed_dialog.cc:~140` — `Valid()` rejects when `in_point_ + consumed_ > media_duration_`, but `consumed_` is fixed at `SetClip` time — a clip already overflowing its media makes the dialog permanently invalid → Apply stays disabled with no recovery.
    *Fix:* validate against `in_point_ + duration*rate` of the *pending* values, or allow trimming.

23. **LOGIC** `ui/effects_panel.cc:172` — `RecomputeFilter` clamps `scroll_y_` to `[0, content_height_]` instead of `[0, content_height_ - view]` → scrolls past the last row into blank space.
    *Fix:* subtract the viewport height (as `Layout` does at line ~299).

24. **LOGIC** `app_runtime/native_modules.cc:286` — `BuildNet` runs `Network::Fetch`/`DownloadFile` synchronously inside the JS function call on the UI/bootstrap thread → whole editor freezes during any fetch; a hanging URL stalls the app.
    *Fix:* make fetch async (enqueue + promise-style completion) or move to a worker.

25. **LOGIC** `ui/browser_panel_widget.cc:23` — `g_pending_url`/`g_pending_title` are file-static globals shared by all `BrowserPanelWidget` instances and persist across widget destruction → a second panel (or a recreated one) receives stale pending navigation/title events.
    *Fix:* scope the queue per widget instance.

26. **LOGIC** `ui/video_editor_app.cc:3622` — `Tick` holds `preview_mutex_` while calling `monitor_->SetExternalFrame` (full-frame memcpy + `ApplyMonitorDisplayTransform` + scope recompute) and `monitor_->SetTime` — engine callbacks (`OnEngineFrame`) block on the same mutex for the entire widget update → frame-drop jitter during playback.
    *Fix:* copy pending values under the lock, release, then apply to widgets.

27. **LOGIC** `ui/history_panel.cc:149` — undo rows display `undo_[undo_count-1-row]` (newest-first, steps=row+1 — consistent), but redo rows display `redo_[row-divider-1]` **front-first** while `on_redo_steps` counts from the next redo step — if `RedoLabels` returns oldest-first (matching `UndoLabels`), the top redo row's label is the *last* step applied by its click → label/action inversion.
    *Fix:* index `redo_[redo_count-1-(row-divider-1)]` to match the undo convention.

---

## LEAK

28. **LEAK** `app_runtime/icon_pack.cc:~55` — `cache_` is an unbounded `unordered_map` keyed by `(name,px,argb)`; `px`/`argb` are caller-chosen → cache grows without bound over a session.
    *Fix:* LRU-cap or bound to known sizes.

29. **LEAK** `ui/timeline_widget.cc:400` — `peaks_cache_` (shared_ptr<AudioPeaks>, potentially MBs each), `media_duration_cache_`, `peaks_missing_`, `poster_missing_` all grow unbounded for the session — only `wave_cache_`/`poster_cache_` are LRU-capped.
    *Fix:* bound the peaks/duration caches or evict on `ConsumeCatalogDirty`.

30. **LEAK** `app_runtime/js_engine.cc:~300` — `impl_->handles` (V8 `Global`s) grows monotonically — `NewObject`/handler wraps push forever with no reclamation → long sessions accumulate V8 globals.
    *Fix:* add handle freeing on object teardown or reuse slots.

---

## STUB / dead paths

31. **STUB** `ui/inspector_widget.cc:~700` — `SeekKey` reports `"on_seek not wired"` when the hook is absent — harmless but the button silently no-ops in non-app hosts.
    *Fix:* hide/disable the seek buttons when `on_seek` is unset.

32. **STUB** `ui/export_dialog.cc:506` — `Escape` is only handled when `finished_`; during `running_` it falls through and returns `false`, so Escape propagates to the app shell mid-export instead of mapping to cancel.
    *Fix:* route Escape→`on_export_cancel` while `running_`.

---

### Notes on verified non-issues
- `video_editor_app.cc` `OnEngineFrame/Peaks/Playhead` ↔ `Tick` handoff is mutex-guarded correctly (the *scope* of the lock is the issue in #26).
- `~VideoEditorAppWidget` calls `preview_engine_.Shutdown()` and `export_engine_.Wait()` before member teardown — lifecycle ordering is sound.
- `lab_panel` `AsyncOp` is cancel-then-join in dtor; `hooks`/`params` are captured by value — safe.
- `mixer_panel` `drag_strip_`/`hovered_strip_` are re-validated in `RebuildStrips`; `SetPeak(-1)` maps to master with empty-check.
- `media_bin` grid/list indexing via `visible_` indirection is bounds-checked; context-menu actions capture `path` by value.
- `markers_panel` `CommitRename` is guarded by `SetProject→CancelRename` (no stale-row rename).
- `js_engine` `CallHandler` bounds-checks `handler_id` before indexing `handles` (#7 is about the unchecked *type*, not the index).
- `monitor`/`scopes` `src_` raw pointer is only dereferenced inside `SetFrame` immediately after assignment — not stale.
- `speed_dialog` `consumed_`-invariant and `export_dialog` finished-state machine are internally consistent.
