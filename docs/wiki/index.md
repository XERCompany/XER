# XER Creative Suite — Wiki Index

Tribal knowledge for the long-running goal: build a professional
creative suite (Premiere Pro + After Effects + Photoshop paradigm) on
the XER runtime, starting with a multi-track video editor in `tools/`.

Goal started: 2026-11-15. Design doc:
`docs/superpowers/specs/2026-11-15-video-editor-design.md`.

## Decisions

- [video-editor-scope.md](decisions/video-editor-scope.md) — every
  user decision from the grilling rounds (2026-11-15), incl. the later
  expansion to full Adobe-system scope.
- [suite-architecture.md](architecture/suite-architecture.md) — one app
  with workspace modes vs separate tools; comp=timeline unification;
  phase roadmap.
- [toolchain-migration.md](decisions/toolchain-migration.md) — proposed
  vcpkg→choco+msys2+clang rebuild (2026-11-15); audit done, sequencing
  vs Phase 1C undecided.
- [process-task.md](decisions/process-task.md) — new tools/process_task
  process/service auditor: heuristics + pluggable reputation,
  GUI table + JSON/CSV export (2026-11-15).

## Research

- [toolchain-migration-audit.md](research/toolchain-migration-audit.md)
  — choco/msys2 package availability table, ABI analysis, corrected
  installer list, repo-side change list, risk-ranked blockers.
- [2026-11-20-gap-analysis.md](reports/2026-11-20-gap-analysis.md) —
  video_editor spec-vs-code gap report: top blocker = synchronous
  decode on UI thread; transition/JKL/keys parity list.
- [2026-11-20-bug-audit-core.md](reports/2026-11-20-bug-audit-core.md)
  — 16 core/infra findings: OIIO extension-dot dead path,
  plugin_host untrusted-pointer crashes, project_store remove+rename
  data-loss, mixer peak race.
- [2026-11-20-bug-audit-ui.md](reports/2026-11-20-bug-audit-ui.md) —
  32 UI findings: CEF-callback UAF, lab-worker unsync reads,
  OOB in peaks/nav_rail, SortDedupeKeys data-loss.
- [2026-11-21-graphics-editor-stack.md](research/2026-11-21-graphics-editor-stack.md)
  — Photoshop-class editor stack research: existing Skia/image/lab
  assets, Krita tile engine blueprint, GEGL/libmypaint/PhotoshopAPI
  verdicts, M1–M5 build order (2026-11-21).
- [2026-11-22-ai-model-stack.md](research/2026-11-22-ai-model-stack.md)
  — AI model stack research: license-clean ship candidates (BiRefNet,
  MI-GAN/LaMa, Real-ESRGAN, EfficientSAM, Depth-Anything-S, RF-DETR),
  license blocklist (Ultralytics AGPL, RMBG, SAM 3), MSYS2/vcpkg ORT
  fix paths (2026-11-22).
- [2026-11-22-modern-nle-features.md](research/2026-11-22-modern-nle-features.md)
  — competitor survey (Resolve 20, Premiere 25-27, CapCut, Descript,
  FCP 11): top-10 ranked list — captions/text-edit/silence/ducking/
  reframe all hang off already-vendored whisper+ONNX models
  (2026-11-22).
- [2026-11-22-registry-color-ux-widgets.md](research/2026-11-22-registry-color-ux-widgets.md)
  — shadcn-registry survey (Colorshot, Amplo, photocn, SureUI, Tour,
  Delta, CalendarCN, MagicUI, Animate UI, ElevenLabs UI) for the
  color/photo/UX-polish widget wave: OKLCH picker + eyedropper,
  risk-matched confirm UX, transcript viewer, motion presets;
  15-row priority table (2026-11-22).
- [2026-11-22-registry-media-widgets.md](research/2026-11-22-registry-media-widgets.md)
  — 10-site shadcn-registry survey for the C++ widget kit: audio-ui.dev +
  audiocn knob/fader/meter-ballistics/peak-waveform APIs, videocn +
  videojs transport chrome, tactile hardware-feel controls, piemenu
  radial menus, zriz cropper; gap list + prioritized widget table
  (2026-11-22).
- [2026-11-21-nle-architectures.md](research/2026-11-21-nle-architectures.md)
  — NLE engine survey: MLT/Kdenlive/Shotcut, Olive node+render cache,
  Blender VSE proxies, OFX/Frei0r/libplacebo/VapourSynth matrix,
  render-cache + proxy blueprints, top-3 calls (2026-11-21).
- [2026-11-22-registry-data-widgets.md](research/2026-11-22-registry-data-widgets.md)
  — 12-site shadcn-registry survey for the widget kit: full API blueprints
  for tree view (G3), scrub number field (G5), virtualized table (G4),
  tags input, stepper, filter bar, inspector; prioritized 17-item port
  table with file names and effort estimates (2026-11-22).
- [2026-11-23-ai-features-wave2.md](research/2026-11-23-ai-features-wave2.md)
  — next AI wave: verified-contract candidates (RIFE 4.25-lite MIT,
  realesr-general-x4v3 BSD-3, YuNet MIT, Depth-Anything-V2-S Apache,
  Silero VAD MIT, BeatThis MIT) + license blocklist additions (demucs
  weights research-only, SCRFD provenance, musdb stems NC); adopt/defer
  table + 3-wave plan (2026-11-23).
- [2026-11-23-hf-org-scan.md](research/2026-11-23-hf-org-scan.md)
  — targeted HF org sweep for the gap areas: verified-license candidates
  (SAM2.1 tracker Apache-2.0 + ONNX, Kokoro TTS Apache, Florence-2 MIT,
  MODNet Apache, Depth-V3 relicense Apache) + verdicts on LTX-Video
  ($10M-cap community license, real ONNX export exists), sam3/sam-audio
  SAM License, Music3/MusicGen (no legal+runnable path); top-5 for next
  wave (2026-11-23).
- [2026-11-23-nle-workflow-anatomy.md](research/2026-11-23-nle-workflow-anatomy.md)
  — official-doc anatomy of Source/Program monitors, transport (JKL,
  play-around, K-held combos), Media Pool vs bin vs filesystem browsers,
  patching/track-targeting and panel chrome across Premiere/Resolve/FCP/
  CapCut/Descript; per-topic ui2 comparison tables + 20-item prioritized
  gap list (S/M/L, owning file per item) (2026-11-23).
- [2026-11-24-cool-effects-tech.md](research/2026-11-24-cool-effects-tech.md)
  — signature-look wave: per-family effect candidates (halation/bloom/
  film kit, warps, stylize, glow, glitch, temporal, texture) with impl
  sketches + effort/wow; tech evals (xfade~58 transitions near-free,
  effects_rack exposure ~9 audio fx free, ACES/OCIO finish, GL-transitions
  MIT, GPU ladder CPU-parallel→Vulkan/libplacebo, OFX/frei0r/VST3/SoundTouch
  licenses checked) + ranked top-10 (2026-11-24).
- [2026-11-24-adobe-render-pipeline.md](research/2026-11-24-adobe-render-pipeline.md)
  — Adobe/BMD render & playback pipeline anatomy: Mercury Playback
  Engine GPU/CPU split, green/yellow/red render bars, I-frame preview
  files, AE RAM/persistent-disk cache + MFR + adaptive-resolution
  ladder, Resolve 3-tier smart/user cache; per-product table + gap
  analysis + ranked tickets T1–T10 (dropped-frame counter, render
  bar, adaptive quality, priority autocache, disk preview tier,
  MFR export) (2026-11-24).
- [2026-11-24-transitions.md](research/2026-11-24-transitions.md) —
  XER-native `xer.tr.*` transition set: GL-transitions source/license
  audit (MIT per-shader, HP BSD-3 page-curl excluded), NLE convention
  targets (Resolve/Premiere signature list), the 15-transition table
  with param schema + port provenance (2026-11-24).
- [2026-11-20-diff-review.md](reports/2026-11-20-diff-review.md) —
  pass-2 review of session's own changes: seqlock ordering bug in
  RenderStore, channel/worker leak in UiRenderHost, nested-transform
  breakage, smoke watchdog race.

## Architecture / verified facts

- [video-pipeline-facts.md](architecture/video-pipeline-facts.md) —
  verified API facts about src/video, src/content/video,
  src/wrapper/ffmpeg, src/ui that any agent touching this goal needs.
- [build-and-test-infra.md](architecture/build-and-test-infra.md) —
  presets, test patterns, XER_Video/Skia linkage traps, FFmpeg DLL
  paths, JSON lib choice, tool registration.
- [render-worker-infra.md](architecture/render-worker-infra.md) —
  UiRenderHost worker pool, RenderStore shared-memory resource
  cache, stream ops, per-element layers/transforms (2026-11-20).
- [static-link-state.md](architecture/static-link-state.md) —
  what's static vs delay-loaded vs dynamic in the MSYS2 preset,
  skia delay-load impossibility, CopyDLL removal (2026-11-20).
- [windows-msys2-gotchas.md](architecture/windows-msys2-gotchas.md) —
  running-exe build locks (Build.ps1 auto-kill), MSYS2↔Windows path
  rules, GUI-subsystem console contract, windres ASCII (2026-11-21).
- [crash-debugging-windows.md](architecture/crash-debugging-windows.md)
  — no gdb/lldb; VeCrashTrace SEH symbolizer, llvm-symbolizer for
  RVAs, dormant minidump handler, 0xC0000374 fast-fail (2026-11-21).
- [live-ui-testing.md](architecture/live-ui-testing.md) — driving the
  real GUI for verification: ui_drive.ps1 (click by exe-name+coords,
  window→PNG screenshot), the launch→wait→click→shot→check→kill loop,
  PostMessage caveats (2026-11-21).
- [nn-backend-stack.md](architecture/nn-backend-stack.md) — backend
  matrix (cv::dnn active, ORT broken-runtime, ggml required, MKL/dnnl
  wired, cudnn/cuda-toolkit stubbed) + 9 dead-path traps + per-backend
  enable paths (2026-11-22).
- [extension-platform.md](architecture/extension-platform.md) —
  .xexrj/.xsjo/.xejslib wire formats, in-process Clang compile,
  LLJIT + LinkModules mesh, ve.core.h v1→v3 service table,
  xer_ext.ve() JS parity, IDEXEJSO IDE (2026-11-21).
- [video-editor-js-fx.md](architecture/video-editor-js-fx.md) —
  JS effects/transitions (`assets/scripts/fx`, `script:` types) and
  the `--media-worker` decode subprocess (2026-11-20).
- [video-editor-lens-sync-curves.md](architecture/video-editor-lens-sync-curves.md) —
  glass/lens refraction effects + bokeh DOF, one-click waveform
  audio sync, keyframe curve editor easings/stagger
- [ve-ui-rebuild-spec.md](architecture/ve-ui-rebuild-spec.md) — VE UI
  rebuild blueprint: NLE 2025-26 layout survey (Resolve 20/Premiere
  25/FCP 11/CapCut/Clipchamp/Descript/Kdenlive), region→widget
  coverage matrix, surface list + paint contract, M1–M4 phases
  (rebuild decision 2026-11-22; legacy `ui/` deleted and `ui2/` made
  the sole shell 2026-11-22).
- [ui-widget-api.md](architecture/ui-widget-api.md) — universal widget
  layout-geometry contract on `src/ui` (Alignment flags, margin,
  indent, scaledContents, fixed size, Move/Resize hints), the
  SetBounds honoring funnel, `Surface` custom-paint widget, and the
  Qt/views mapping table (2026-11-22).
- [chromium-views-port-plan.md](architecture/chromium-views-port-plan.md)
  — Chromium `ui/views`↔`src/ui` mapping (View↔Widget, FlexLayout↔
  our flex helpers, Widget/NativeWidget↔Window, FocusManager↔tree
  focus), adopt/keep lists, and ranked port tickets T1–T16 with
  effort estimates + "don't port the compositor" verdict
  (2026-11-23).
- [packaging-module.md](architecture/packaging-module.md) —
  `src/packaging` app bundler: in-process PE dep scan, staging tree +
  xer-package/1 manifest, zip via XER_Archive, software-rendered
  `xer_packager` GUI/CLI; archive_manager .xer-tmp format-detection
  fix (2026-11-24).
- [chromium-render-port-plan.md](architecture/chromium-render-port-plan.md)
  — Chromium render/media pipeline ↔ VE render path mapping
  (VideoFrame↔video::Frame, BeginFrame↔WM_TIMER pump, VRA↔none,
  SharedImage↔RenderStore/keyed-mutex), binary-verified facts
  (libskia=Ganesh-GL-only, libEGL=real ANGLE-D3D11, FFmpeg has
  d3d11va), per-frame CPU-copy cost audit, single-process zero-copy
  design (d3d11va→VideoProcessor→GPU compositor→keyed-mutex present),
  and ranked tickets R1–R9 + not-portable list (command_buffer/Mojo/
  GPU-process) (2026-11-24).
- [xer-sync-dev.md](architecture/xer-sync-dev.md) — `xer_sync_dev`
  tool for `third_party/3RD.xersyunc`: status/sync/cmake-gen/verify/
  auto/selftest subcommands, libclang include-graph analysis (MSYS2
  clang64 `libclang` via `find_package(Clang)`), `xer-<dep>.cmake`
  patch shapes, `.xersync-state.json` interval gating (2026-11-25).
- [content-opendialog.md](architecture/content-opendialog.md) —
  `src/content/opendialog`: Windows common-item-dialog pickers
  (open/save/dir read+write) with `IFileDialogCustomize` custom widgets
  embedded in the system dialog, legacy comdlg32 fallback, HWND seam
  for `src/ui`, and the headless/interactive test split (2026-11-24).
- [builder-check-2026-11-15.md](reviews/builder-check-2026-11-15.md) —
  independent design-doc review findings and how each was resolved.

## Progress

- [phase-1a-engine-foundation.md](progress/phase-1a-engine-foundation.md)
  — Phase 1A COMPLETE, **merged to main** (`a0f5058`); commits,
  rulings, handoff items for 1B-1E.
- [phase-1b-timeline-core.md](progress/phase-1b-timeline-core.md)
  — Phase 1B COMPLETE, **merged to main** (`b966af9`); model/history/
  store/catalog/posters + hardening.
- [2026-11-20-working-version-push.md](progress/2026-11-20-working-version-push.md)
  — smoke mode (14 checks), config.h generation, FFmpeg bridge
  fix, check_all.ps1, wave-2/3 queue.

- [2026-11-22-api-max-and-photo-m1.md](progress/2026-11-22-api-max-and-photo-m1.md)
  — three waves landed: compositor→Skia 29-mode swap + PCM→AudioCore;
  NN dead-path cleanup (MKL active, ORT still stale-binary); Photo M1
  core (TiledDevice COW + memento undo + dab brush) + open decisions.
- [2026-11-21-lens-sync-icons.md](progress/2026-11-21-lens-sync-icons.md)
- [2026-11-21-nle-redesign.md](progress/2026-11-21-nle-redesign.md) —
  Premiere-style visual redesign (4 waves): new theme tokens,
  monitor/transport, timeline chrome, right-dock panels (2026-11-21).
- [2026-11-21-gpu-ai-modules.md](progress/2026-11-21-gpu-ai-modules.md) —
  YA.CUDADecode/CUDAEncode/FrameRender + YA.AIFindObject + clip AI
  tags; RTX 3070 nvenc/cuda-hwaccel verified, ORT dll broken →
  OpenCV DNN active (2026-11-21).
  — lens/glass FX + waveform sync + curves, exe icons/metadata,
  GUI-subsystem console rules

## Audit reports
- [2026-11-22-src-api-reuse-audit.md](reports/2026-11-22-src-api-reuse-audit.md)
  — tools/-vs-src/ duplication: compositor→Skia bridge top swap
  (7→29 blend modes), OIIO→ImageLoader, PCM→AudioCore, plugin-kernel
  strategy options (2026-11-22).
- [2026-11-21-gap-and-env-audit.md](reports/2026-11-21-gap-and-env-audit.md)
  — ranked remaining gaps: sync-on-UI-thread, JS fx hazards, drag&drop,
  unsaved-changes; env: build locks, CI smoke, --diag, screenshot tests

## Conventions

- Claims are dated. When code changes, add a new dated claim and mark
  the old one superseded — never silently rewrite.
