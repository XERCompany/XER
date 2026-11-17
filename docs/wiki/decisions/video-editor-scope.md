# Video Editor — scope decisions

All decisions from the grilling interview, 2026-11-15. User answered
rounds 1–4, then approved the design ("Все ок"), then expanded scope
("Скопировать систему After Effects, Premiere Pro, Photoshop" +
"ДАЛЬШЕ").

## Round 1 — form & API

- **Form**: standalone app in `tools/` (like `tools/preview`), NOT a
  widget in the main app, NOT JS/addon, NOT node_graph.
- **API layer**: both — `src/content/video` (`VideoEngine`) for preview,
  `src/video` (`VideoManager`/`FrameExporter` family) for render/export.
- **MVP loop**: full timeline — add files, order, trim in/out, preview,
  export to video file.
- **Audio**: FULL — audio track, mixing, sound in export (not
  preview-only).

## Round 2 — model & depth

- **Tracks**: full multi-track (multiple video + audio tracks), not
  audio-follows-video.
- **Persistence**: JSON project file + SQL (SQLite) — resolved as
  JSON = project file, SQL = media catalog/index (not dual-format).
- **Transitions + effects**: crossfade AND wipe/slide transitions;
  `VideoEffects` per clip.
- **Preview model**: smart-player — active clip under playhead via the
  decode pipeline; NOT full timeline compositing (later upgraded, see
  below).

## Round 3 — architecture forks

- **A/V mux**: extend `XER::Bridge::FFmpeg` with a two-stream mux
  function (one `AVFormatContext`, video+audio providers, PTS
  interleave). Not two-pass, not deferred.
- **Transforms**: full affine — offset + scale + rotation, constant
  per clip (keyframes were still excluded at this point).
- **Preview audio**: only the active clip's audio (no realtime mix).
- **Transitions UI**: crossfade + wipes + slides (MixSystem covers
  blend; wipes/slides need new geometry).

## Round 4 — editing depth

- **Animation**: constant transform per clip — NO keyframes (at that
  time).
- **Audio depth**: per-clip gain + mute + fade in/out; audio
  crossfades at transitions. No track gain/pan.
- **Effects**: chain per clip, curated subset of `VideoEffects`.
- **Canvas**: explicit project settings (w/h/fps/bg), default from
  first clip.
- **Edit ops**: move, trim edges, split at playhead, delete, snap —
  NO ripple/slip/slide/roll.
- **Export**: fixed defaults H.264+AAC→mp4 (question skipped → rec).
- **Undo**: yes, ~50 steps (node_graph_history pattern).
- **Clip posters**: yes, one poster per clip at import.

## Scope expansion — 2026-11-15 (later same day)

User: "Скопировать систему After Effects, Premiere Pro, Photoshop".
Answers to the follow-up:

- **UX+tools of ALL THREE apps** (not just Premiere shell): Source
  Monitor, Program Monitor, Effects Controls, Media Browser, tool
  strip — Premiere depth; AE panels/controls; PS layers/masks paradigm.
- **Photoshop = real image-editing** inside the suite (layers,
  adjustment layers, filters — not just the paradigm metaphor).
- **After Effects = keyframes + motion graphics IN SCOPE** — this
  REVERSES the earlier "no keyframes" non-goal.
- **Wiki location**: `docs/wiki/` (this folder).

## Builder-check resolutions (my calls, 2026-11-15)

- Transition geometry: window centered on the cut, extends `duration/2`
  into each side reading **handle frames** from source media beyond
  in/out points (standard NLE), clamped to file bounds.
- `clip.muted` = audio-only mute; video tracks get `hidden` flag;
  audio tracks get `muted`.
- Preview fidelity: raw active clip by default + "Full composite"
  quality toggle running the compositor (Premiere playback-resolution
  analogue).
- Audio-only files: supported — requires relaxing
  `Bridge::FFmpeg::VideoDecoder::Open` to accept audio-only
  containers (it currently fails "no video stream").
- Export decoders: per-clip instances (not per-path).
- `.xerproj` extension; catalog at `%LOCALAPPDATA%/XER/VideoEditor/`.
- Headless `video_editor --export in.xerproj out.mp4` in scope.
- Phase-1 tool strip: selection/razor/hand/zoom/rate-stretch; slip/
  slide/roll stay Phase 2.
