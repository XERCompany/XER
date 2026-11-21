# 2026-11-21 — Lens/glass FX, audio sync, curves, exe resources

Session outcome: three user-requested feature blocks + Windows
packaging (icon, metadata, no console) landed; smoke 17/17, tests
green. Commits `6bd3da3`..`5a82ec8` (HEAD `5a82ec8`).

## Landed (user-visible)

- **Glass/lens native effects** in compositor dispatch: `lens_sphere`
  (sphere-normal refraction, bend>1.2 inverts like a real ball,
  fresnel rim + glint), `magnifier`, `ribbed_glass` (fluted),
  `bokeh_dof` (CoC by distance from focus zone, 24-tap Vogel disc,
  luma-weighted bokeh balls), `bokeh_blur`. Pure bilinear gathers —
  GPU variant is a mechanical port, tracked follow-up.
- **Waveform audio sync** (`audio_sync.{h,cc}`, Timeline menu):
  energy-envelope NCC at ~21ms cells → confidence-gated moves through
  `TimelineHistory::MoveClip` (undoable). Reference = selected clip
  else earliest on lowest track.
- **Curve editor**: KeyEase +6 (CubicIn/Out, ExpoOut, Back, Elastic,
  Bounce); dbl-click cycles all; Ctrl+C/V shared clipboard across
  properties; Ctrl+S staggers other properties' keys.
- **Windows resources** (`xer_app_win32` cmake fn + `app.rc.in`):
  icon.ico (ffmpeg from icon.jpg) + VERSIONINFO (Author "Matvey
  Vladimirovich (Yoshi Arasaka & LXXV)", Company "XERLab") on 14
  shipped exes; `-mwindows` non-Debug → no console flash; stdout kept
  alive via `GetFileType`+`AttachConsole` fallback in main.cc.

## Bugs fixed on the way

- `model2` test used `"bounce"` as invalid ease — now valid; test
  switched to `"wobble"`.
- AttachConsole v1 rebound stdout to CONOUT$ unconditionally →
  silently swallowed piped `--smoke` output; fixed by skipping when
  `GetFileType(stdout)` is PIPE/DISK.

## Pitfalls worth remembering

- Running `video_editor.exe`/`xer_engine_ui_render.exe` lock CEF
  deploy + link; kills needed before rebuild (repeated 3x today).
- windres mangles non-ASCII in string values — keep ASCII.
- `--media-worker`/CEF `--type=` must never AttachConsole (stdout IS
  the IPC channel).

## Quick-wins batch (same day, subagent-delivered, uncommitted)

Working tree at report time: 13 files, +619/-58. Smoke **20/20 OK**.

- A/V demux path now respects the audio-tap high-water mark (drops at
  the queue TAIL, not the front); `dropped_audio_packets` counter
  warns once on 0->1 (`ffmpeg_video_decoder.cc`).
- `PrintUsage` documents `--audio-probe`/`--media-worker`.
- New smoke checks: `probe_classification` (9-case cover-art table),
  `poster_cache_lru` (LRU promotion/byte accounting),
  `assets_icons` (yaml/icons/.xexrj deploy audit).
- UX: unsaved-changes prompt (dirty flag + MessageBoxW + WM_CLOSE
  subclass veto), PageUp/PageDown + Shift+M marker nav,
  ApplyProjectSettings now goes through history (undoable).
- Env: `Build.ps1 -KeepRunning`/auto lock-kill (path-scoped),
  CI runs `--smoke`, `CopyDirRetry` names the locker, wiki
  superseded-banner + `windows-msys2-gotchas` +
  `crash-debugging-windows` pages, `.clangd` repointed to the
  windows-clang-msys2 compile DB.


## M-batch (same day, subagent-delivered, uncommitted)

Smoke **22/22 OK**. Working tree ~+2293/-199 across 25 files.

- **audio_sync** runs off the UI thread with progress+cancel; peaks via
  MediaPrep (subprocess + disk cache); correlation decimate-coarse-
  refine bounded ~2e8; whole sync = one BeginMacro undo step;
  smoke check verifies lag->shift sign.
- **JS fx**: FxWatchdog TerminateExecution budgets (500ms video /
  100ms audio / 1s load); FormatV8Error ToLocal fallback; per-clip
  `ctx.clip_id` state (delay/lowpass keyed by it); shared_mutex
  catalog snapshot; hostile-script smoke coverage.
- **`--diag`**: paste-able env dump (PE subsystem, assets boot, WASAPI
  init, FFmpeg lib versions, CEF presence, catalog rows, delay-load
  inventory, smoke list, XER_* env). Caught missing
  libshaderc_shared/libwhisper-1 delay-loads on this machine.
- **Drag&drop**: WM_DROPFILES via the existing close-guard subclass
  (dirs expand one level, media filter shared with the open dialog);
  bin-card drag -> timeline drop at pointer time (grid mode, mouse
  capture; first matching unlocked track).


## Design + capture batch (same day, subagent-delivered, uncommitted)

Smoke **24/24 OK**. Working tree ~+2873/-274 across 31 files.

- **Object Lab panel** rebuilt on Design UI: ButtonGroup mode strip,
  labeled Fields (X/Y/W/H/Feather), Field-caption live slider readout,
  Separator/section-header conventions from Inspector; Enter-in-field
  runs the op; contract unchanged.
- **`--screenshot`** lands BOTH paths: program-frame (headless
  Compositor -> PNG via FrameExporter) AND full-UI capture via
  `SoftwareRenderer(nullptr)` rasterizing the real widget tree with
  `VideoEditorAppWidget::PrepareUiCapture(t)` presenting a live frame
  into the monitor. `--ui` flag, `--size`, `--t`, `--out`; new
  `screenshot_png` smoke check. This closes the last manual
  verification gap — UI screenshots can now be diffed in CI.


## Design-system restyle (same day, 5-wave subagent batch, uncommitted)

Smoke **24/24 OK** after all waves; verified visually via `--screenshot --ui`.

- **Groundwork**: `ui/icon_helpers.{h,cc}` (IconPack raster helpers +
  `InstallButtonIconResolver` — Button can't depend on tool-level
  IconPack, so `Button::SetIconResolver` is DI);
  `Button::SetIcon` (16 DIP, left-of-label/icon-only); `BadgeVariant::
  Destructive`; `VeTheme::kScrim`/`kOverlayChip`; dead `track_header`
  deleted.
- **Dialogs**: shortcuts overlay -> Kbd + ScrollArea; settings fps chips
  -> ButtonGroup; title align -> ButtonGroup; speed -> Field+SetError.
- **List panels**: verified in widget_tree.cc — Click/MouseMove are
  target-only (only MouseWheel bubbles), Item has no on_click — so
  effects/plugins/color kept drawn rows but got tokens/icons/clip-fix;
  markers/history/export-queue used the **disabled-widget trick**
  (disabled Item/Badge/Separator render but skip hit-testing, so panel
  clicks still land).
- **Shell**: right tab strip -> `Widgets::Tabs` + TabStripScroll;
  media-bin filters -> ButtonGroup; scopes tabs -> ButtonGroup;
  nav rail + both monitor transports -> icon-only Buttons;
  mixer mute -> Destructive Button.
- **Known limits** (for future waves): Click doesn't bubble (a
  src/ui/widget_tree change would unlock Item-with-click rows);
  `Badge` had no Destructive until this batch; Tabs has no per-tab
  tooltips; timeline/monitor surfaces stay custom canvas (intended).


## Icons + process grouping batch (same day, uncommitted)

Smoke 24/24 OK after the batch.

- **Icon pack**: icons.yaml rewritten to 47 Material Symbols mappings
  (+18 new SVGs from google/material-design-icons), real play_arrow
  glyph; manifests' icon names now resolve.
- **WinJobGroup** (`src/helper/win_job.h`): per-PID Job Object,
  KILL_ON_JOB_CLOSE; media-worker, engine renderers, VST3 scan probes
  and watch-recompile children assigned -> killing the editor reaps
  workers (fixes orphan bin-locks). Crash-restart child + CEF
  deliberately excluded (must outlive parent / own job).
- **Naming**: FileDescription now Title-Case per target
  ("VideoEditor - XER Creative Suite" etc. via xer_target_desc() in
  CMakeLists); window title + SetAppName match. InternalName/
  OriginalFilename kept = real exe name. Task Manager shows friendly
  names; exes NOT renamed (hard-referenced).
- **Rounded icons**: icon.ico rebuilt as multi-size RGBA ICO
  (16/32/48/256, 22% squircle radius, SDF AA mask); icon.jpg =
  squircle on #1e1e24 matte.
- Known limit: 16px wordmark illegible (source lockup has margins);
  future pass could use bare X glyph for small frames.


## Process decomposition batch (same day, uncommitted)

Smoke **26/26 OK** (new checks: `media_worker`, `sync_worker`, `fx_worker`).

- **Dedicated worker exes** on XER_VideoEditor_Core:
  `XERVideoLoader.exe` (posters/peaks/media-info — the old
  --media-worker role), `XERSyncMedia.exe` (whole sync plan:
  decodes peaks itself + bounded correlation), `XERFxScript.exe`
  (V8 host: fx_list/fx_validate/fx_audio/fx_transition).
- `worker_pipe.{h,cc}` — generic spawn-by-name JSON-lines client
  extracted from MediaWorkerClient; sibling-exe lookup + self-spawn
  fallback + env kill-switches (`XER_VE_SYNC_WORKER=0` etc.);
  `KillWorker()` = the audio-sync cancel mechanism (deliberate kill
  has no death penalty).
- Fallbacks: missing exe -> self-spawn `--media-worker` (loader) /
  in-process (sync plan, audio fx). Runaway JS now kills
  XERFxScript, not the editor.
- Limits: video-fx frames stay in-process (b64/pipe too slow);
  transitions in-process in editor; fx_validate unused by UI yet.
- All workers under WinJobGroup (die with editor); FileDescriptions
  registered in xer_target_desc.


## Bubbling + cancel + diff-batch (same day; partially committed in 281d465)

Smoke **27/27** (new: `ui_screenshot_diff`).

- `widget_tree.cc`: Click now bubbles up GetParent() when the hit
  target returns false (MouseMove stays target-only, deliberate).
  `Item` gained SetOnClick/SetOnDoubleClick; effects/plugins/color
  panels converted to ScrollArea + Item rows.
- Audio-sync Cancel: SyncAudioClips moved off the UI thread onto a
  Lab::AsyncOp (blocking poll removed — the old cancel flag was dead
  code); status-strip Cancel button + Esc -> cancel flag +
  AsyncOp::RequestCancel + SyncMediaPipe().KillWorker(); late plans
  dropped, live-project re-resolve.
- `ui_screenshot_diff` check: program-frame baseline PNG
  (tools/video_editor/tests/baselines/) with mean-abs/hot-pixel
  tolerances; full-UI baseline rejected (font rasterization too
  machine-dependent); XER_VE_UPDATE_BASELINE=1 refreshes.


## Layers/Text/LUT/URL/MediaBrowser batch (same day, uncommitted)

Smoke **32/32 OK** (new checks: `generated_layers`, `text_style_ext`,
`lut_manager`, `url_import`).

- **Generated layers**: Clip::name added; timeline empty-lane context
  menu (Add Color Matte/Text/Bars/Media Here at lane+time), New Layer
  dialog, LAYERS dock panel, TrackAtDropPoint -> bin drops land on
  target lane; serialization round-trips verified.
- **Text FX**: `text_style.{h,cc}` full ClipTextStyle (family/weight/
  slant, letter+line spacing, 9-way align, box+radius, stroke, shadow,
  underline/strike, opacity); title_editor rebuilt as classic text
  inspector on DS widgets; `font_catalog` enumerates 132 families via
  SkFontMgr; `font_manager` dialog (search/styles/live preview).
- **LUT**: `lut_manager` scans luts/ dirs, parses .cube, publishes
  into shared OCIO profile table (new mutex'd Mutate/FindCopy API in
  color/ocio_transform — fill-once-at-boot was racy).
- **URL import**: `url_import` uses Url::URLRequestJob for direct
  http(s); yt-dlp (installed via pacman, clang64) handles platform
  URLs w/ progress + WinJobGroup; File-menu + bin-menu entries;
  dialog is Field/TextInput modal.
- **MediaBrowser**: search box, grid/list toggle, Select sort,
  kind chips on tiles, real min/max waveform strips on audio tiles
  (MediaPrep peaks, LRU'd), footer counters.


## Perf + live-verify + test-fix batch (same day, uncommitted)

Smoke 32/32 + XER_VideoEditor_Tests 20 fails -> 0.

- **Preview-quality**: Compositor::SetPreviewQuality threaded
  monitor quality menu -> PreviewEngine worker. Heavy fx render on
  downscaled frame + upscale; bokeh_dof 1912 -> 124ms at quarter.
  Full path also micro-optimized (Vogel tables, row early-out) and
  stays PIXEL-IDENTICAL (smoke asserts). Hidden --bench-effects flag.
- **URL import live-verified**: real HTTP+HTTPS+redirects+CD headers
  + real YouTube yt-dlp download all pass. Bugs fixed: effective-URL
  naming (URLRequestJob gained effective_url out-param),
  `-f best` -> `bv*+ba/b` (modern YT has no merged formats), .cube
  CRLF stripping (all shipped LUTs are CRLF).
- **Test suite**: stale bitmap-font expectations branched on
  XER_VE_HAS_SKIA (propagated to test target); stale "XER Studio"
  title expectation updated.
