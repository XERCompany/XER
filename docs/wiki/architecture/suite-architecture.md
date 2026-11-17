# Creative Suite — architecture & phasing

Status: proposal (my call, 2026-11-15 — user said "ДАЛЬШЕ" on maximal
scope; flagged for correction).

## Vision

One application — working name **XER Studio** — with three workspaces
sharing shell + infrastructure:

| Workspace | Adobe analogue | Core |
|---|---|---|
| Cut | Premiere Pro | multi-track timeline, trims, transitions, program/source monitors, export |
| Motion | After Effects | comps (= timelines), keyframes, effects controls, masks, shape/text layers |
| Photo | Photoshop | layer stack on stills, adjustment layers, filters, masks |

## Why one app, not three binaries

- AE comps ARE timelines with keyframes — the Phase-1 timeline model
  extends, not forks.
- PS layers ARE the same layer/stack/mask/blend-mode primitives
  (`MaskSystem`, `MixSystem::Blend`, `CompositingEngine`) applied to
  stills.
- One shell = one panel-dock system, one history/undo infra, one media
  catalog, one export engine (still export = 1-frame "video").
- Splitting later is easy (modules are separate); merging later is not.

## Proposed layout

```
tools/creative/            (or keep tools/video_editor for Phase 1)
  shell/        app bootstrap, workspace switcher, dockable panels,
                shortcuts map, theme
  timeline/     model + history + JSON store (Phase 1 core)
  preview/      smart-player + composite-preview toggle
  compositor/   frame-accurate renderer (decode→fx→transform→stack→
                transition→canvas)
  audio/        PCM mixer → mux provider
  export/       export engine + progress/cancel + atomic write
  catalog/      SQLite media index + posters
  cut/          Premiere workspace widgets (timeline UI, monitors,
                tool strip, effects controls)
  motion/       AE workspace (Phase 2): keyframe editor, comp panel,
                shape/text layers
  photo/        PS workspace (Phase 3): layer panel, adjustments,
                filter gallery
```

Phase 1 may keep the `tools/video_editor` name/dir and grow into the
suite; renaming is a builder call at plan time.

## Phases

- **Phase 1 — Cut (NLE core)**: everything in the design doc +
  Premiere-grade shell: dockable panels, workspace presets, tool strip
  (selection/razor/hand/zoom/rate-stretch), Source Monitor, Program
  Monitor, Effects Controls panel, Media Browser, keyboard shortcuts,
  dark theme. Slip/slide/roll remain Phase 2.
- **Phase 2 — Motion (AE)**: keyframe tracks on transform/opacity/
  effect params (linear/ease interpolation), graph editor, masks on
  clips (MaskSystem exists), shape/text layers, render queue.
- **Phase 3 — Photo (PS)**: still-image mode — layer stack, blend
  modes, adjustment layers, filter gallery (VideoEffects set +
  Skia-only ones), PSD-like project format or reuse .xerproj.

## Cross-cutting environment work (pit of success)

- `XER_Video` is an orphan — Phase 1 makes it a real dep; fix link
  issues once, benefits all phases.
- New bridge API `EncodeAudioVideoStream` + tests — needed by Phase 1,
  reused by all.
- Panel/dock system designed once in Phase 1 shell — Phase 2/3 plug in.
- Shortcuts map as data (JSON) — remappable, consistent across
  workspaces.
