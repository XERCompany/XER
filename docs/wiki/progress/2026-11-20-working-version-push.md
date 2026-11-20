# Progress: "make it really work" push (2026-11-20)

Coordination log for the multi-agent effort on video_editor +
config.h. Sources: subagent reports in this session.

## Landed (committed)

- **`video_editor --smoke` / `--smoke-list`** (`cf7bb61` + follow-up):
  14 machine-checkable checks — timecode, sequence presets,
  timeline model/history, keyboard map, export presets,
  generated_source, compositor_render, audio_mixer, project_store,
  project_autosave, media_catalog, app_boot (assets→V8→window.yaml),
  export_render (real libx264 MP4). `SMOKE <name> OK/FAIL`, exit
  0/1, 15s watchdog, files under `%TEMP%/xer_video_editor_smoke/`.
  All 14 green on 2026-11-20.
- **FFmpeg bridge fix** (same wave): `ENGINE_HAS_FFMPEG_*` defs were
  INTERFACE-only on `XER_FFmpeg` and never reached
  `XER_FFmpeg_impl` → `YA.MediaDecoderUniversal.dll` shipped stub
  bridge functions; `--export` and `MediaCatalog::Import` probing
  were silently broken in the YA-DLL preset. Fixed by adding the
  defs PRIVATE to the impl target in
  `src/wrapper/ffmpeg/CMakeLists.txt`.
- **Configure-generated `src/config.h`** (`93273a7`): template
  `src/config.h.in` + `cmake/config_checks.cmake`; 37 `XER_HAVE_*`
  macros probed via `check_include_file[s]` —
  WinAPI (combined `windows.h;<h>` probes for non-self-contained
  headers), POSIX, macOS (OBJCXX probes for Cocoa/AVFoundation),
  C++ STL. Real values on windows-clang-msys2 include
  `XER_HAVE_SYS_MMAN_H 0` (mingw CRT has no mman.h) and
  `XER_HAVE_JTHREAD 0` (libc++ there lacks it).
- **`tools/check_all.ps1`** — repeatable check harness:
  XER_UI_Tests / XER_Media_Smoke / EngineScanVST3 always;
  video_editor --smoke / sd / preview unless `-Quick`.

## Completed 2026-11-20 (waves 2-3)

- **Media-prep worker**: `MediaPrep` pool (2 threads), dedup+back-
  pressure queue, results → `PosterCache` + peaks store; widget hooks
  tri-state (`kReady/kPending/kFailed`), `Tick` drains dirty flags →
  repaint. Tests: `tests/video/video_editor_media_prep_tests.cc`.
- **Quick wins**: 12-type transition picker (junction menu +
  inspector type/duration), full JKL shuttle → `SetRate`, Q/W
  ripple-trim, `,`/`.` nudge ±1f, Ctrl+D default transition.
- **Core audit fixes (15/15)**: plugin_host params validation +
  retire-list for DLLs; audio_pcm bounds + rate latch;
  `Helper::Path::AtomicReplace` (project_store + export_preset);
  MacroCommand rollback; OIIO extension normalize (stills path
  un-dead); media_prep purge-generation; atomic peaks; OCIO
  call_once; ext_host unload; generated_source overflow guard.
- **UI audit fixes (11/11)**: CEF-callback weak_ptr sink, lab
  snapshot context, peaks bounds, nav_rail index guard, OIIO thumb
  shared_ptr, SharedBootEngine invalidation, graveyard rebuilds,
  SortDedupeKeys keep-last, right_tab 0..9, svg Attr boundaries,
  duration-probe eviction.
- **Pass-2 diff-review fixes (7/7)**: seqlock ordering in
  RenderStore, kind/payload validation on resource ops, worker
  dispatch try/catch, purge TOCTOU closed, StreamRenderer
  release→pool, transform stack + RAII unwind in widget_tree,
  cancellable smoke watchdog.
- **`ocio_transform` exit 127**: `xer_stage_runtime_dlls` helper +
  `EnsureOcioRuntime`/`EnsureOiioRuntime` LoadLibrary probes.
- **sd.exe**: ggml runtime staged (22 DLLs incl. dlopen'd backends).
- **`check_all.ps1` pipe-deadlock fixed**: async pipe drain before
  WaitForExit (was the phantom XER_UI_Tests timeout).
- **Final verification**: `check_all.ps1` 6/6 PASS —
  XER_UI_Tests (94), Media_Smoke, EngineScanVST3,
  video_editor --smoke (14), sd --help, preview.

## Completed 2026-11-20 (waves 4-5)

- **Remaining UI-audit fixes (agent c4eb9a24)**: js_engine
  `As<Function>` guard + handle free-list reclamation; monitor
  `SetExternalFrame` size check; `fx_disabled_` survives rebuilds
  via basis+ordinal remap; markers dedupe in store; keyframe
  index-shift on erase; speed-dialog dead-Apply fixed via clamp
  fallback; effects-panel scroll clamp; BuildNet timeout caps
  (30s/120s — async needs engine promise support, documented);
  icon_pack FIFO cap 256; timeline caches bounded; Tick
  `preview_mutex_` swap-out pattern; export-dialog Escape→cancel;
  `RequestImportPoster` drop counter + rate-limited status warn.
- **Source Monitor (agent 371af282)**: `source_edit.{h,cc}` —
  `SourceMarks`/`ResolveSourceRange`/`MakeSourceClip`/
  `SourceEditAt` (insert/overwrite, Premiere-style clamp);
  `ui/source_monitor_widget.{h,cc}` — seek ruler, in/out brackets,
  transport row, timecode footer; second `PreviewEngine` decoding a
  synthetic one-clip `Project` (real decoded pixels); I/O marks
  per-path; JKL/arrows/Space/,/. route to last-touched monitor
  (`monitor_target_`); exclusive transport; View→Source Monitor
  toggle; `EditSourceToTimeline` splits straddlers inside a macro
  (undoes on failure). Smoke: 15 checks (`source_edit` added).
- **Final verification**: `check_all.ps1` 6/6 PASS —
  XER_UI_Tests, Media_Smoke, EngineScanVST3,
  video_editor --smoke (15), sd --help, preview.

## Queued (next)

- Dockable panels + remappable shortcuts (spec parity, larger
  chunks).
- JS `BuildNet` true-async needs an engine promise kind in
  `OutValue` (documented constraint).
- `config.h` dual-toolchain clobber — generated into source tree
  per user request; two presets overwrite each other's values
  (last configure wins).

## Process note

Commits to this working tree happen from an external process —
agents observed their changes swept into `93273a7`/`cf7bb61`
mid-flight. Treat `git log` as shared history, don't expect a clean
commit boundary per task.

## Completed 2026-11-20 (waves 6-7)

- **Audio silence — true root cause** (agent 8be92390):
  `CoInitializeEx(MT)` on the STA UI thread → RPC_E_CHANGED_MODE →
  sink null since launch. Fixed + per-engine WASAPI sinks (shared
  sink was thread-unsafe). Linked audio clips: A/V → video+audio
  pair in link_group; mixer suppresses embedded audio of linked
  video clips (no double decode).
- **Render providers** (agent e0aa8a8f): `renderer_provider.h`
  backends kAuto/kSoftware/kD2D/kD3D11/kVulkan,
  `XER_UI_RENDERER` env, fallback chain. Real Vulkan backend
  (no SDK — dynamic vulkan-1.dll load, 3 pipelines) verified on
  RTX 3070; software renderer retained-buffer. 106/106 UI tests.
- **CEF + VST3 view** (agent 50a6e277): BrowserPanelWidget DPI fix
  (DIP→px + clip); VST3 editor rewritten as owned resizable popup
  (title bar, WM_CLOSE→Hide, IPlugView::getSize, param commit-back
  to clip.audio_fx via history on close); per-row Editor buttons
  in Inspector; `video_editor.exe --cef-check` — real browser +
  load event. Gaps: no IPlugFrame; editor instance ≠ live mixer node.

## Completed 2026-11-20 (waves 7-9)

- **Flicker fix** (agent 48c85630): root cause = native-res posters
  (~8.3 MB each) in the 64 MiB PosterCache → ~7 slots vs dozens of
  250 ms buckets → LRU thrash → visible blink. Fix: `kPosterMaxEdge`
  320 px area-average downscale before caching (~285 entries).
- **Cover-art classification**: `StreamInfo.attached_pic` +
  ProbeVideo skips attached_pic / "cover" metadata / ≤2-frame
  image-codec streams beside audio — tagged mp3/flac → audio-only.
- **CEF + VST3 view** (agent 50a6e277): browser DPI fix; VST3 editor
  = owned resizable popup with param commit-back via history;
  `--cef-check` flag verifies real CEF in-process.
- **Full UI redesign** (agent 092e81cb): `ui/ve_theme.{h,cc}` design
  tokens (violet accent #8B5CF6 from editor.yaml, spacing 4-24px,
  radius 4-8, fonts 11/13/18); `Window::SetTokens/ClearTheme` in
  src/ui; timeline chrome bands, kind-tinted track headers w/ badges
  + M/H/L chips, two-tone rounded clips w/ label strip + link
  indicator, refined ruler + playhead handle; panel chrome/spacing
  everywhere.
- **SEH crash tracer** in tests/ui/test_main.cc — any future crash
  prints module+RVA+symbolized stack instead of bare exit code.
- **False-alarm diagnosis**: XER_UI_Tests segfault under check_all
  was an ABI skew (AudioFrameInfo grew channel_layout mid-commit;
  consistent rebuild → green, 3x3 runs clean).
- Final: `check_all` 6/6 PASS; smoke 16/16; UI tests 106+.

## Completed 2026-11-20 (wave 10) — "звук не идет" resolved

- **ROOT CAUSE (heap corruption 0xC0000374)**: `VideoDecoder::Open`
  used `av_find_best_stream(AVMEDIA_TYPE_VIDEO)` which returns the
  `attached_pic` cover-art stream as "video" → mjpeg pumped through
  the sws path → heap corruption → mixer worker / process death →
  silence. Probe isolated it: plain mp3 = 141/141 writes OK, same
  mp3 + attached_pic = crash. Fix: skip `AV_DISPOSITION_ATTACHED_PIC`
  in stream selection in BOTH `ffmpeg_video_decoder.cc` and
  `ffmpeg_engine_bridge.cc::OpenDecoderContext` — decode side now
  agrees with ProbeVideo classification (mp3+cover = audio-only).
- **Late-binding audio sink**: `PreviewOptions::audio_sink` is a raw
  pointer captured at Init — a device appearing later (retry,
  headphones plugged in) could never reach an already-running engine.
  Added `LateBindingAudioSink` forwarder (atomic target swap);
  engines hold it for life, `EnsurePlatformAudioSink` binds on
  success.
- **Sink retry actually runs**: `EnsurePreviewEngine` early-returns
  once ready, so the throttled retry never fired — moved into Tick
  (retries only for initialized engines missing a sink).
- **Timeline thumbnail flicker (real fix)**: widget-level
  `poster_cache_` was insertion-order eviction — hits did not promote,
  so >48 visible buckets evicted each other → per-frame kPending →
  blink. Now a true LRU (touch-on-hit, cap 160) + nearest-poster
  fallback: a pending/missed slot draws the closest cached frame of
  the same media instead of blanking.
- **Diagnostics**: `video_editor --audio-probe [file]` — end-to-end
  PreviewEngine→mixer→real WASAPI run printing per-link verdicts
  (encode/probe/sink/init/writes/verdict); SEH crash tracer added to
  the video_editor binary too (worker crashes now print module+RVA
  + symbolized stack).
- Stale catalog rows self-heal: re-import upserts probe fields
  (mp3 misclassified before the cover fix → re-import → audio-only).
- Final: `check_all` 6/6 PASS; probe cover mp3 = 141 writes,
  0 fails, all non-silent.
