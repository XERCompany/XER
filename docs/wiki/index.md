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

- [2026-11-21-lens-sync-icons.md](progress/2026-11-21-lens-sync-icons.md)
- [2026-11-21-nle-redesign.md](progress/2026-11-21-nle-redesign.md) —
  Premiere-style visual redesign (4 waves): new theme tokens,
  monitor/transport, timeline chrome, right-dock panels (2026-11-21).
  — lens/glass FX + waveform sync + curves, exe icons/metadata,
  GUI-subsystem console rules

## Audit reports
- [2026-11-21-gap-and-env-audit.md](reports/2026-11-21-gap-and-env-audit.md)
  — ranked remaining gaps: sync-on-UI-thread, JS fx hazards, drag&drop,
  unsaved-changes; env: build locks, CI smoke, --diag, screenshot tests

## Conventions

- Claims are dated. When code changes, add a new dated claim and mark
  the old one superseded — never silently rewrite.
