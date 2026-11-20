# Gap analysis: video_editor vs spec (2026-11-20)

Subagent audit of `tools/video_editor` (~46k lines) against
`docs/superpowers/specs/2026-11-15-video-editor-design.md` and
`2026-11-17-video-editor-phase2.md`. Read-only findings; file/line
references as of `cf7bb61`.

## Verdict

The pipeline is real, not faked — Phase 1 + most of Phase 2 landed,
plus beyond-spec extras (VST3 chains, OCIO, generated
matte/bars/text, adjustment layers, AI markers, extensions).
`generated_source.cc` covers only procedural kinds; real media
decodes via `IMediaDecoder` → FFmpeg bridge.

## Top blockers (ranked by "breaks usability")

1. **Synchronous FFmpeg decode on the UI thread** — posters
   (`timeline_widget.cc:499-526`), audio peaks (`:398-411`,
   `video_editor_app.cc:1928-1948`), import poster
   (`video_editor_app.cc:1244-1253`). `media_catalog.h:99-103` itself
   warns "call from a worker". `poster_cache.*` exists (thread-safe
   LRU) but is dead code — only tests consume it. → fix in flight
   (media-prep worker, wave 2).
2. **Transition vocabulary unreachable** — UI hardcodes Crossfade
   @1.0s (`video_editor_app.cc:1639-1646`,
   `timeline_widget.cc:1420-1426`, `inspector_widget.cc:178-183`)
   though model+compositor support ~12 types.
3. **JKL shuttle unwired** — `PreviewEngine::SetRate` never called
   from UI; J/K/L seek ±5s + pause (`video_editor_app.cc:3139-3155`).
4. **Missing spec keys** — Q/W ripple-trim, ,/. nudge ±1f, Ctrl+D
   default transition (`RippleTrimClip` exists, unbound).
5. **No Source Monitor** (`media_bin_widget.cc:167` — intentionally
   dropped), no dockable panels, non-remappable shortcuts
   (`keyboard_map.cc` is display-only), no cross-track drag,
   no unsaved-changes prompt.

## Verified NOT broken (skip re-checks)

Export mux + atomic commit (`export_engine.cc:263-268,524-569`),
`av_rescale_q` stream-units fix, Skia straight-alpha blend,
audio-tap contract, CLI `--export/--info`, WASAPI preview sink,
VST3 chain, generated sources silent in mixer, 21 editor test
files under `tests/video/`.

## Recommended next chunk (accepted, in flight)

Media-prep worker: bounded thread pool feeding `PosterCache` +
peaks store; `PeaksFor`/`PosterFor` → lookup-or-enqueue with
placeholder until data lands; import poster via same worker;
cancel on clip removal. Cheap follow-on: transition type+duration
picker on junction menu + JKL→`SetRate` + missing keys.
