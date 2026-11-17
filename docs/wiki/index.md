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

## Architecture / verified facts

- [video-pipeline-facts.md](architecture/video-pipeline-facts.md) —
  verified API facts about src/video, src/content/video,
  src/wrapper/ffmpeg, src/ui that any agent touching this goal needs.
- [build-and-test-infra.md](architecture/build-and-test-infra.md) —
  presets, test patterns, XER_Video/Skia linkage traps, FFmpeg DLL
  paths, JSON lib choice, tool registration.
- [builder-check-2026-11-15.md](reviews/builder-check-2026-11-15.md) —
  independent design-doc review findings and how each was resolved.

## Progress

- [phase-1a-engine-foundation.md](progress/phase-1a-engine-foundation.md)
  — Phase 1A COMPLETE, **merged to main** (`a0f5058`); commits,
  rulings, handoff items for 1B-1E.
- [phase-1b-timeline-core.md](progress/phase-1b-timeline-core.md)
  — Phase 1B COMPLETE, **merged to main** (`b966af9`); model/history/
  store/catalog/posters + hardening.

## Conventions

- Claims are dated. When code changes, add a new dated claim and mark
  the old one superseded — never silently rewrite.
