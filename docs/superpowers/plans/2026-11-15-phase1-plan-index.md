# Phase 1 "Cut" — Plan Index

Spec: `docs/superpowers/specs/2026-11-15-video-editor-design.md`
Verified facts: `docs/wiki/architecture/video-pipeline-facts.md`,
`docs/wiki/decisions/video-editor-scope.md`

Phase 1 is decomposed into five sub-plans. Only **1A** has a full
task-level plan today (`2026-11-15-phase1a-engine-foundation.md`); the
rest are scoped here so their interfaces and ordering are fixed before
implementation starts.

## Dependency graph

```
1A Engine foundation
  └─► 1B Timeline core ──► 1C Compositor + export ──► 1D Cut UI ──► 1E Integration
        └──────────────────────────────────────────► 1D
```

1D consumes the 1B model/history/store/posters directly and hard-depends
on 1C only for the export dialog + "Full composite" preview — land 1C
first or stub those two touchpoints.

## 1A — Engine foundation

**Goal:** prove `XER_Video` links as a real consumer, relax
`VideoDecoder::Open` for audio-only containers, and add the
`EncodeAudioVideoStream` A/V mux bridge API with round-trip tests.

- Plan: `2026-11-15-phase1a-engine-foundation.md`
- Creates: `tests/video/xer_video_smoke.cc`,
  `tests/ui/ffmpeg_audio_tap_tests.cc`, `tests/ui/ffmpeg_av_mux_tests.cc`
- Modifies: `src/wrapper/ffmpeg/ffmpeg_engine_bridge.{h,cc}`,
  `src/wrapper/ffmpeg/ffmpeg_video_decoder.{h,cc}`,
  `tests/CMakeLists.txt`
- Produces (exact spec types): `EncodeAudioVideoParams`,
  `AudioFrameStreamProvider`, `EncodeAudioVideoStream(...)` in
  `XER::Bridge::FFmpeg`; audio-only-capable `VideoDecoder::Open`.
- Consumed by: 1C (mux + per-clip audio-tap decode for export), 1B
  (`clip_poster` uses `VideoDecoder` + `FrameExporter`).
- Why first: everything downstream either links `XER_Video` or calls
  the new bridge API; orphan-lib breakage must surface here, not inside
  editor code.

## 1B — Timeline core (headless)

**Goal:** project model, undo/redo, JSON store, SQLite media catalog,
and clip posters — all testable without UI or FFmpeg encode.

- Creates (under `tools/video_editor/`): `timeline_model.{h,cc}`,
  `timeline_history.{h,cc}`, `project_store.{h,cc}`,
  `media_catalog.{h,cc}`, `clip_poster.{h,cc}` + unit tests (likely
  `tests/video/` or a tool-local test exe — builder call at plan time).
- Produces (spec data model, namespace TBD — proposed
  `XER::VideoEditor`): `TrackKind`, `Transform`, `ClipEffect`, `Clip`,
  `Transition`, `Track`, `Project`; `TimelineHistory` mirroring
  `XER::NodeGraph::History` (`Command{Apply/Revert/Label/MergeWith}`,
  `History{Execute/Undo/Redo/BeginMacro/EndMacro}`, depth ~50);
  `.xerproj` JSON v1 load/save; `MediaCatalog` over
  `XER::SQL::Storage::SQLiteDatabase` at
  `%LOCALAPPDATA%/XER/VideoEditor/catalog.db`; `ClipPoster` extraction.
- Consumes: 1A (`VideoDecoder`+`FrameExporter` for posters; audio-only
  probe path), `XER::Content::Video::ProbeVideo`, `XER_SQL`, `XER_Cache`,
  a JSON lib (jsoncpp or nlohmann — pick at implementation time).
- Why after 1A: poster extraction and media probing exercise the same
  decode paths 1A validates; model/history/store themselves are pure
  and could be stubbed in parallel, but keeping one linear order avoids
  rework when probe APIs settle.

## 1C — Compositor + export

**Goal:** frame-accurate offline render — decode → effects → affine
transform → track stack → transitions → canvas BGRA, mixed PCM audio,
single-file A/V mux, atomic write, progress/cancel, `--export` CLI.

- Creates: `tools/video_editor/compositor.{h,cc}`,
  `audio_mixer.{h,cc}`, `export_engine.{h,cc}`; extends `main.cc`.
- Produces: `Compositor::RenderFrameAt(double t, …)` → canvas BGRA
  (exact signature is a builder call within the spec's description);
  `AudioMixer` PCM-chunk provider; `ExportEngine` worker consuming
  `EncodeAudioVideoStream` + `EncodeVideoFrameStream` /
  `EncodeAudioFrames` for A-only/V-only projects; `--export in.xerproj
  out.mp4`.
- Consumes: 1A mux API + audio-tap decoder; 1B `Project`/`Clip`/
  `Transition`/`Track` snapshot; `video::Frame`, `MixSystem`,
  `VideoEffects` (CPU-path subset), `XER::Cache::TempPathForTarget` +
  `CommitTempFile`.
- Why after 1B: the compositor and mixer are pure functions of the
  timeline model — they cannot be written before the model exists.

## 1D — Cut workspace UI

**Goal:** Premiere-style dockable shell — tool strip, Source/Program
monitors, timeline widget, Effects Controls, Media Browser, shortcuts,
export dialog, workspace presets, dark theme.

- Creates: `tools/video_editor/` UI sources per spec layout
  (`preview_engine`, `monitor_widget`, `timeline_widget`,
  `media_bin_widget`, `inspector_widget`, `export_dialog`, shell/dock
  + shortcuts map) on `XER_UI_Core` widgets.
- Produces: `PreviewEngine` (pool of `XER::Content::Video::VideoEngine`
  per clip id, LRU); all editor widgets bound to 1B model +
  `TimelineHistory`; `ExportDialog` driving 1C `ExportEngine`.
- Consumes: 1B (model, history, store, catalog, posters), 1C
  (`Compositor` for the "Full composite" preview toggle,
  `ExportEngine` for export). `VideoPreviewWidget` is the reference
  for hosting `VideoEngine`.
- Why after 1B/1C: every widget mutates through `TimelineHistory` and
  reads the 1B model; export UI needs 1C's engine. Draft-quality preview
  needs only `VideoEngine`, so 1D can start once 1B lands and stub the
  two 1C touchpoints — but shipping order should be 1C → 1D.

## 1E — Integration / polish

**Goal:** wire CLI+GUI end-to-end, smoke tests, docs, builder-call
cleanup (effect list, wipe/slide backend, JSON lib, extension).

- Modifies: `tools/video_editor/*`, root `CMakeLists.txt` (final
  target shape per spec ~line 460), `docs/`.
- Consumes: everything above.
- Exit criteria from spec: `--info`/`--export` CLI works;
  import→trim→crossfade→export→plays smoke passes; `XER_Video` remains
a clean linked dep; wiki updated with dated claims.
