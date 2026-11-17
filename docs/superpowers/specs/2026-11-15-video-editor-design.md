# XER Studio — Phase 1 "Cut" (NLE) Design Document

Date: 2026-11-15 (revised same day after scope expansion to the
Premiere/After Effects/Photoshop system)
Status: Scope approved; Phase 1 spec

Program context: this is **Phase 1** of a creative suite — see
`docs/wiki/architecture/suite-architecture.md` (vision, phasing) and
`docs/wiki/decisions/video-editor-scope.md` (full decision log).
Keyframes/motion graphics (Phase 2) and image editing (Phase 3) are
program scope, NOT non-goals — they are simply later phases.

## Intent

A standalone non-linear video editor — the "Cut" workspace of the
future XER creative suite — built on the existing `src/` media APIs. It
lives in `tools/` next to `tools/preview`, uses the custom XER UI
widget framework with a Premiere-style dockable-panel shell, and
produces real exported video files (H.264 + AAC in MP4) with a
multi-track timeline, transitions, per-clip effects, affine transforms,
and audio mixing.

The "why": XER already has a complete decode/composite/encode pipeline
(`src/content/video`, `src/video`, `src/wrapper/ffmpeg`) but no NLE on
top of it. `XER_Video` is currently an orphan static lib — nothing links
it. VideoEditor is the consumer that justifies and exercises it.

## Goals

- Import local media files into a media bin / catalog
- Multi-track timeline: video tracks (z-ordered overlay) + audio tracks
  (mixed)
- Clips: move, trim in/out, split at playhead, delete, snap
- Per-clip: gain/mute, fade in/out, affine transform
  (offset + uniform scale + rotation — constant per clip, no keyframes),
  chain of effects from a curated set
- Transitions between adjacent clips on the same video track:
  crossfade + directional wipes + slides, with duration
- Undo/redo (~50 steps) for all timeline edits
- JSON project files (save/load) + SQLite media catalog
- Poster frame per clip, extracted at import
- Smart-player preview: active clip under the playhead plays through the
  existing decode pipeline; preview audio = active clip's audio only;
  "Full composite" quality toggle runs the real compositor per frame
- Premiere-grade shell: dockable panels, workspace presets, tool strip
  (selection/razor/hand/zoom/rate-stretch), Source Monitor + Program
  Monitor, Effects Controls panel, Media Browser, remappable shortcuts,
  dark theme
- Offline frame-accurate export: video via `EncodeVideoFrameStream`,
  audio mixed to PCM, single-file A/V mux (new bridge API), atomic
  temp→commit write, progress + cancel
- Headless CLI export: `video_editor --export in.xerproj out.mp4`

## Non-goals (Phase 1)

- Keyframe animation, motion graphics, shape/text layers → **Phase 2**
- Image editing mode (layers/adjustments on stills) → **Phase 3**
- Ripple/insert edits, slip/slide/roll edits → Phase 2
- Track-level gain/pan, preview-time mixing of multiple audio tracks
- Video scopes, color wheels, LUTs
- Titles/text overlays, still images as timeline clips
- Retiming / speed changes (clip `duration` always equals
  `out_point - in_point`; no time-stretch — rate-stretch tool affects
  monitor zoom interpretation only if trivially mappable, else defer)
- Network/URL sources (local files only)
- Cross-platform video pipeline (Windows/MSVC only, like the rest of
  the video stack)
- Codecs other than the default export (H.264 + AAC → .mp4); the bridge
  already takes codec names, UI exposes none

## Anti-goals

- Realtime-capture export (playback-speed-dependent, dropped frames) —
  export is strictly offline frame-accurate
- Loading whole timelines into RAM — video export streams frames through
  `VideoFrameProvider`; audio streams PCM chunks
- Modifying `XER_Video` renderers (CUDA/Vulkan/ANGLE) — export renders
  on CPU into BGRA; GPU backends stay unused

## Constraints (verified facts)

- Video/FFmpeg pipeline is **Windows/MSVC-only**: FFmpeg bridge symbols
  are gated behind `if(MSVC)` in `src/wrapper/ffmpeg/CMakeLists.txt`;
  `VideoAudioPlayer` is WASAPI/`_WIN32`. The tool gets a GUI on Windows
  and a `--info` CLI fallback elsewhere (pattern: `tools/preview`).
- **No A/V mux exists**: `EncodeVideoFrames`/`EncodeVideoFrameStream`
  write a single video stream; `EncodeAudioFrames` writes a single
  audio stream. Each call creates its own `AVFormatContext` with one
  `avformat_new_stream`. A new bridge function is required (below).
- **No frame-accurate seek for export**: bridge
  `VideoDecoder::Seek` = keyframe-backward + flush ONLY (no forward
  discard — ffmpeg_video_decoder.cc:447-478). The ≤120-frame discard +
  ~0.1s slop lives in the Content layer `VideoSession::ApplySeek`
  (video_session.cc:15,282-296). For exact trims the export decoder
  must decode sequentially and filter by PTS.
- **Audio tap starvation**: `ReadAudioFrames` only receives packets
  while `NextFrame()` pumps the demuxer
  (ffmpeg_video_decoder.cc:424-431); packet queue ≤2048, drops oldest
  on overflow (lines 117-124). The audio export path must pump video
  decode concurrently per clip.
- **`VideoDecoder::Open` rejects audio-only files** ("no video stream",
  ffmpeg_video_decoder.cc:239-243) — Phase 1 must relax this to accept
  audio-only containers (audio tap then works; `NextFrame` returns
  false), or fall back to `AudioSourceLoader`/`DecodeAudioFrames`
  (batch, no seek) for short files.
- `src/video` (`XER_Video`) is an orphan: `add_subdirectory(src/video)`
  exists (root `CMakeLists.txt:159`) and the target builds, but no
  binary links it — and it **references Skia symbols unconditionally**,
  so it won't link without the Skia port (present on Windows via
  `XER_UI_Core`'s required `unofficial-skia`).
- Several `VideoEffects` are **Skia-only with no CPU fallback** —
  Sharpen, Vignette, Pixelate, EdgeDetect, Emboss, ChromaticAberration,
  OilPaint, MotionBlur silently no-op when Skia is unavailable
  (video_effects.cc:111-186). The curated effect set must either stick
  to CPU-fallback effects or accept the Skia requirement.
- `VideoDecoder::ReadAudioFrames`/`EnableAudioTap` exist only on
  `XER::Bridge::FFmpeg::VideoDecoder`, not on the `Content::Video`
  adapter — audio export uses the Bridge decoder directly.
- `CompositingEngine::Layer` supports only `z_order`, `x/y` offset,
  `opacity` — no scale/rotation. The editor's compositor needs its own
  affine layer transform (Skia or manual resample+rotate).
- `RunFilterGraph`, `Transcode`, `CaptureDeviceToStream` in the bridge
  are **mocks** — do not use them.
- UI framework: retained-mode widget kit (`src/ui`), widgets implement
  `Render/Render/Layout/HandleEvent/Tick`; frames drawn via
  `Renderer::DrawFrameBgra`. `VideoPreviewWidget` is the reference for
  hosting `VideoEngine`.

## Architecture

```
tools/video_editor/
  main.cc                 CLI (--info/--version) + GUI bootstrap
  timeline_model.{h,cc}   Project/Track/Clip/Transition data + queries
  timeline_history.{h,cc} undo/redo (Command pattern, cf. node_graph_history)
  project_store.{h,cc}    JSON save/load
  media_catalog.{h,cc}    SQLite index of imported media
  clip_poster.{h,cc}      poster extraction + disk cache
  preview_engine.{h,cc}   smart-player transport over active clip
  monitor_widget.{h,cc}   preview viewport
  timeline_widget.{h,cc}  tracks UI: clips, trim handles, playhead, snap
  media_bin_widget.{h,cc} imported media list
  inspector_widget.{h,cc} clip transform/gain/effects/transition editors
  export_dialog.{h,cc}    export options + progress + cancel
  export_engine.{h,cc}    offline render worker (video + audio + mux)
  compositor.{h,cc}       frame-accurate frame renderer (decode→fx→
                          transform→stack→transition→canvas BGRA)
  audio_mixer.{h,cc}      PCM mix of audio-bearing clips → provider
```

All editor logic is UI-thread-owned except `export_engine` (worker
thread) and `preview_engine` (uses `VideoSession`'s own decode worker).
The timeline model is only mutated on the UI thread; export snapshots it
at start.

## Data model

```cpp
enum class TrackKind { Video, Audio };

struct Transform {            // constant per clip; normalized units
    float offset_x = 0, offset_y = 0;   // fraction of canvas w/h
    float scale = 1.0f;                 // uniform
    float rotation_deg = 0;
    float opacity = 1.0f;
};

struct ClipEffect {
    std::string type;          // "brightness_contrast" | "grayscale" |
                               // "box_blur" | "sharpen" | "vignette" |
                               // "pixelate" (curated set — final list
                               // is a builder call within VideoEffects)
    std::vector<float> params; // per-type parameter vector
};

struct Clip {
    std::string id;            // uuid
    std::string source_path;
    int track = 0;             // index into Project::tracks
    double timeline_start = 0; // seconds
    double in_point = 0;       // source seconds
    double duration = 0;       // timeline seconds; out = in + duration
    bool muted = false;
    float gain_db = 0;
    double fade_in = 0, fade_out = 0;    // seconds
    Transform transform;       // default: fit-to-canvas letterbox
    std::vector<ClipEffect> effects;     // applied in order
};

struct Transition {
    std::string from_clip, to_clip;      // must be adjacent on same track
    enum class Type {
        Crossfade, WipeLeft, WipeRight, WipeUp, WipeDown,
        SlideLeft, SlideRight, SlideUp, SlideDown
    } type = Type::Crossfade;
    double duration = 1.0;               // seconds, centered on the cut
};

struct Track {
    TrackKind kind = TrackKind::Video;
    std::string name;
    bool muted = false;    // audio tracks: silence; video tracks: see hidden
    bool hidden = false;   // video tracks: not composited (audio still plays
                           // unless muted); ignored on audio tracks
    float gain_db = 0;     // stored for Phase 2 use
};

struct Project {
    int width = 1920, height = 1080;
    int fps_num = 30, fps_den = 1;
    std::array<uint8_t,4> bg_color = {0,0,0,255};
    std::vector<Track> tracks;
    std::vector<Clip> clips;
    std::vector<Transition> transitions;
    double duration() const;             // max clip end
};
```

Track ordering: video tracks render bottom→top in `tracks` order;
audio tracks all mix. Default transform for a new clip = uniform scale to
fit canvas + center (letterbox), preserving aspect.

### Model invariants (enforced by all edit commands)

- Clips on the same track never overlap; `move`/`trim` clamp or reject
  to preserve this (transitions require adjacency).
- Trim clamps to media bounds: `in_point ∈ [0, duration - clip.duration]`.
- Audio-only files (`ProbeVideo` → width/height = 0) may only be placed
  on audio tracks; video tracks require a video stream.
- `split` at playhead divides the clip and re-points/remaps any
  transition that referenced it.
- A `Transition` is valid only while its two clips remain adjacent on
  the same track; edits that break adjacency drop the transition
  (undoable like any other change).
- `clip.muted` silences the clip's **audio only** — its video still
  composites (Premiere semantics). Track-level: video `hidden` removes
  video+its embedded audio from composite but the clip's audio may
  still be heard only if not muted — simplest consistent rule:
  `hidden` video track ⇒ clip contributes neither video nor audio;
  `muted` audio track ⇒ silence; clip `muted` ⇒ silence.
- Transition geometry (standard NLE handles): the transition window is
  centered on the cut and extends `duration/2` past each clip's
  in/out point **into the source media** (handle frames). Source time
  clamps to file bounds; when handles run out the transition shortens
  or the edge freezes — never reads outside the file.
- The catalog DB is global across projects (path-keyed), not per-project.

## JSON project format

`.xerproj` JSON (extension is a builder call):

```json
{
  "version": 1,
  "canvas": {"width": 1920, "height": 1080, "fps_num": 30, "fps_den": 1,
             "bg_color": [0,0,0,255]},
  "tracks": [{"kind": "video", "name": "V1", "muted": false, "gain_db": 0}],
  "clips": [{"id": "...", "source": "D:/media/a.mp4", "track": 0,
             "timeline_start": 0.0, "in_point": 1.5, "duration": 10.0,
             "muted": false, "gain_db": -3.0, "fade_in": 0.5,
             "fade_out": 0.0,
             "transform": {"offset_x": 0, "offset_y": 0, "scale": 1.0,
                           "rotation_deg": 0, "opacity": 1.0},
             "effects": [{"type": "box_blur", "params": [4.0]}]}],
  "transitions": [{"from": "clipA", "to": "clipB",
                   "type": "crossfade", "duration": 1.0}]
}
```

## SQLite media catalog

`XER::SQL::Storage::SQLiteDatabase` (raw SQL API is sufficient).
DB location: `%LOCALAPPDATA%/XER/VideoEditor/catalog.db` (builder call on
exact path). Schema:

```sql
media(id INTEGER PK, path TEXT UNIQUE, duration REAL, width INT,
      height INT, fps REAL, video_codec TEXT, audio_codec TEXT,
      has_audio INT, file_size INT, mtime INT, probe_json TEXT,
      poster_path TEXT, added_at INT);
```

`poster_path` points at an image next to the DB (`<id>.jpg`).
`ImageToJpeg` only decodes the FIRST frame of a file — the poster
pipeline is: `VideoDecoder` seek to mid-clip → `NextFrame` →
`FrameExporter::ToFile` (PNG) or BGRA→JPEG encode. Catalog is a
cache/index — deleting it loses nothing but re-probe time.

## Preview engine (smart player)

- `PreviewEngine` owns a small pool of `XER::Content::Video::VideoEngine`
  instances keyed by clip id — `VideoEngine` (not raw `VideoSession`)
  because it already wires the decode worker, command buffer, frame
  cache, **and** the `VideoAudioPlayer` attach that `VideoSession`
  alone does not do. Pool covers the active clip plus neighbors;
  lazily opened, LRU-closed.
- Transport maps timeline position → clip-local source time:
  `src_t = clip.in_point + (t - clip.timeline_start)`.
- Play: `engine->Play()` on the clip under the playhead — the
  **topmost unmuted clip across all tracks** (embedded audio of a video
  clip counts on its track). Pause/scrub: `Pause()` + `Seek(src_t)`.
- At clip end during playback: advance playhead to next clip's start,
  switch engine, continue — gap between clips = paused monitor on last
  frame (or black).
- Transitions are NOT previewed in the default mode (accepted
  simplification): during a transition window the incoming clip is
  already "active" once the playhead crosses the cut point.
- **Preview quality toggle** (Premiere playback-resolution analogue):
  `Draft` = raw active clip (default, fast); `Full` = run
  `Compositor::RenderFrameAt` per UI frame — shows transforms,
  overlays, transitions, effects WYSIWYG at reduced fps. Audio stays
  the active-clip path in both modes.
- Audio-only clips on audio tracks: no `VideoEngine` (it needs a video
  stream) — preview plays their audio via a Bridge decoder +
  `VideoAudioPlayer` attach; monitor shows a placeholder/waveform.
- Monitor widget draws `GetLatestFrame()` via `DrawFrameBgra`,
  letterboxed — same approach as `VideoPreviewWidget`.

## Export pipeline (offline, frame-accurate)

Worker thread (`export_engine`), UI shows progress + Cancel.

**Video pass** — drives `Bridge::FFmpeg::EncodeVideoFrameStream` (or the
new mux below) through a `VideoFrameProvider`:

1. `total_frames = round(duration * fps)`.
2. Frame `n` → output time `t = n / fps`.
3. `Compositor::RenderFrameAt(t, canvas)`:
   - find video clips active at `t` (including transition windows);
   - per clip, ensure decoded source frame at
     `src_t = in_point + (t - timeline_start)`:
     per-clip `Bridge::FFmpeg::VideoDecoder` decodes **sequentially
     forward**, discarding frames until `frame.pts ≈ src_t` (nearest,
     tolerance = half frame duration). Initial position uses `Seek()`
     then forward-discard — never relies on seek precision;
   - apply clip `effects` chain to the clip's BGRA frame
     (`VideoEffects` statics; resize/rotate via a small software path —
     Skia if `XER_VIDEO_HAS_SKIA`, else stb-style fallback is a builder
     call);
   - apply `transform` → affine layer;
   - resolve transition at `t`: crossfade = `MixSystem::Blend(a, b,
     Alpha, weight(t))`; wipe/slide = geometric mask/offset mix —
     implement in `compositor` with `MaskPointsVector` +
     `FragmentCutter` or direct pixel lerp (builder call, CPU is fine);
   - stack tracks bottom→top with `CompositingEngine`-equivalent logic
     (own layer list — `CompositingEngine::Layer` lacks scale/rotation,
     so either extend it or keep layers inside `compositor`);
   - paint `bg_color`, output canvas-size BGRA.
4. Provider converts BGRA → `VideoFrameInfo` (I420 via the bridge's
   existing sws path — the provider hands BGRA like
   `EncodeVideoFrameStream` already accepts).

Decoder lifecycle: **one decoder per clip instance**, opened lazily on
first needed frame and closed once the clip's last output frame has been
produced (per-path sharing would thrash seeks when two clips from the
same file sit far apart on the timeline).

**Audio pass** — `audio_mixer` produces `AudioFrameInfo` chunks for the
mux provider:

- For every clip with audio (video clips' embedded audio + audio-track
  clips that are audio-only files): open a
  `Bridge::FFmpeg::VideoDecoder` with `EnableAudioTap(true)`,
  `Seek(in_point)`, then **pump `NextFrame()` concurrently** while
  draining `ReadAudioFrames` — packets only flow while the demuxer is
  pumped, and the 2048-packet queue drops oldest on overflow. For
  audio-only files this requires the bridge relaxation noted in
  Constraints (or `AudioSourceLoader` for short files).
- Mix into a timeline PCM buffer: per-sample sum of
  `gain * fade_envelope(t) * sample`, clipped; sample rate = project
  rate (48000), stereo float→s16 for AAC.
- Audio crossfade at transitions: the same `weight(t)` ramp applies to
  both clips' audio gains inside the transition window — including
  handle-region audio (transition window extends past clip bounds per
  the handle model).
- `clip.muted` / track `muted`/`hidden` contribute silence per the
  semantics in Model invariants.
- Edge case: a project with **no video clips** exports audio-only via
  the existing `EncodeAudioFrames` (no mux needed); a project with no
  audio clips uses `EncodeVideoFrameStream` alone. The mux path runs
  only when both exist.

**Mux** — new bridge API (the one missing piece):

```cpp
struct EncodeAudioVideoParams {
    EncodeVideoParams video;   // existing struct
    EncodeAudioParams audio;   // existing struct
};
using AudioFrameStreamProvider =
    std::function<bool(std::size_t frame_index, AudioFrameInfo* out,
                       std::string* error)>;

bool EncodeAudioVideoStream(
    const EncodeAudioVideoParams& params,
    std::size_t video_frame_count, const VideoFrameProvider& video_provider,
    std::size_t audio_frame_count, const AudioFrameStreamProvider& audio_provider,
    std::string* error_out);
```

Implementation notes for the builder: one `AVFormatContext`, two
`avformat_new_stream` (video + audio), `avformat_write_header`, then
interleave by computed PTS (video `n/fps`, audio chunk index * chunk
duration) writing whichever is due first via `av_interleaved_write_frame`,
`av_write_trailer`. Reuse the existing encode internals
(`ffmpeg_engine_bridge.cc` ~lines 1473-2155) — extract shared
encoder/stream setup rather than duplicating. Audio start offset:
`VideoDecoder::GetAudioStartSeconds()` — preserve via negative CTS/edit
list or shift audio PTS so A/V align at t=0 (builder verifies with a
real file; `-c copy` remux is NOT an option, we encode).

**Atomic write**: `XER::Cache::TempPathForTarget` +
`CommitTempFile` (pattern from `music_bass_eq_clip.cc:570-586`).

**Defaults**: `libx264` + `aac`, `.mp4`, CRF-equivalent bitrate,
project canvas/fps. Export dialog shows path + summary only (Q19 → rec A).

## Undo/redo

`TimelineHistory` mirrors `XER::NodeGraph::History`: `Command` objects
with `Apply(TimelineModel&)/Revert(TimelineModel&)`, `Execute/Undo/Redo`,
`BeginMacro/EndMacro` for drag/trim gestures (coalesced by
`MergeWith`). Depth ~50. Every mutation path (widget edits, inspector
fields, transitions) goes through `History::Execute` — no direct model
writes.

## UI layout (Premiere-style shell)

```
┌────────────────────────────────────────────────────────────────┐
│ app title bar · workspace switcher (Cut | Motion | Photo — later)│
├──────────┬──────────────────────────────┬──────────────────────┤
│ tool     │ Source Monitor │ Program     │ Effects Controls     │
│ strip +  │ (clip preview, │ Monitor     │ (selected clip:      │
│ Media    │  in/out marks) │ (timeline   │  transform, fx chain,│
│ Browser  │                │  output)    │  gain/fades)         │
│          ├──────────────────────────────┤                      │
│          │        timeline              │                      │
│          │  ruler + tracks + clips +    │                      │
│          │  playhead · transitions      │                      │
└──────────┴──────────────────────────────┴──────────────────────┘
```

- **Dockable panels**: media bin, source/program monitors, timeline,
  effects controls, history — drag/dock/float like Premiere;
  workspace presets ("Editing", "Effects", "Minimal").
- **Tool strip**: selection (V), razor (C), hand (H), zoom (Z),
  rate-stretch — cursor-mode driven edits on the timeline.
- **Source Monitor**: preview a media-bin clip with in/out marking
  before placing it (reuses the preview engine on the source file).
- **Shortcuts**: Space play/pause, J/K/L shuttle, arrow-nudge clip,
  Ctrl+Z/Y, C razor-at-playhead, Delete, snap toggle (S) — remappable
  via a shortcuts map (data-driven, JSON).
- Timeline clip blocks render poster thumbnail (left) + name +
  duration; trim handles on edges; transition badge at cuts. Drag =
  macro-undo gesture; snap to clip edges + playhead + 0.

## Build integration

Root `CMakeLists.txt`, next to `preview` (~line 428):

```cmake
add_executable(video_editor tools/video_editor/main.cc <all .cc>)
target_include_directories(video_editor PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/tools/video_editor)
target_compile_features(video_editor PRIVATE cxx_std_23)
target_link_libraries(video_editor PRIVATE
    XER_UI_Core XER_ContentVideo XER_Video XER_FFmpeg XER_SQL
    XER_Cache XER_Helper XER_Flux_Core)
# plus a JSON lib for project_store — jsoncpp or nlohmann_json,
# whichever the repo prefers (both already used in-tree)
if(WIN32) # GUI sources are Windows-gated like preview
endif()
set_target_properties(video_editor PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
```

`XER_Video` linking is the first real consumer — expect to fix minor
orphan-lib issues (missing includes, unused-dep warnings). GUI sources
gated `#if defined(_WIN32)`; non-Windows builds get `--info` only.

## Testing / verification

- Unit: timeline model queries (active clips at t, transitions,
  duration), JSON round-trip, history undo/redo, audio mix gain math —
  plain asserts, no framework needed (match `tests/` style).
- Bridge: `EncodeAudioVideoStream` round-trip — generate synthetic
  frames + sine PCM → export → `ProbeMedia` shows 2 streams;
  `DecodeVideoFrames`/`DecodeAudioFrames` sanity on output. Pattern:
  `tests/ui/ffmpeg_video_decode_tests.cc`.
- Smoke: `tools/video_editor --info` CLI; manual GUI pass — import two
  clips, trim, crossfade, export, verify output plays (ffprobe/VLC).
- `demo_app/` smoke not required (tool, not engine change) — but the
  bridge addition is engine surface; keep its tests in `tests/`.

## Risks / open issues

- **`XER_Video` orphan**: first link may expose build breakage in
  GPU-backend stubs — budget fix time; worst case, `compositor` uses only
  `video::Frame`/`MixSystem`/`VideoEffects` and we can trim unused TUs.
- **Affine transform cost**: per-pixel rotate on CPU for every frame is
  the hot loop. Mitigate: transform via Skia (`XER_BridgeSkia` already
  optional dep of `XER_Video`) or integral fast-path when
  rotation=0/scale=fit. 1080p30 export ≈ real-time×several — acceptable
  for v1, document expected export speed in UI.
- **VFR / rotated sources**: decoder outputs display-rotation-agnostic
  frames; rotation metadata may be ignored — note as limitation.
- **Audio/video drift in mux**: AAC frame sizes (1024 samples) vs video
  fps — interleave by PTS with a small audio FIFO; verify A/V sync on a
  60s export.
- **Long exports**: audio PCM for full duration in RAM is too big —
  stream chunks through the provider (frame_index → render chunk on
  demand), never materialize the whole mix.
- **Skia-only effects**: several `VideoEffects` silently no-op without
  Skia — the compositor should detect and warn (or the curated set
  excludes them); `XER_Video` won't link at all without the Skia port.
- **Audio-only sources**: preview AND export depend on relaxing
  `VideoDecoder::Open` for audio-only containers — small bridge change,
  but it touches shared decode code; keep it minimal and tested.
- **Audio tap throughput**: pump-concurrent decode per audio clip adds
  CPU cost on long exports — measure; if hot, batch-decode audio ranges
  instead of realtime pumping.
- **Suite scope risk**: Phase 2/3 (keyframes, motion graphics, photo
  mode) are program scope — Phase 1 must not paint itself into a
  corner (e.g. keep `Transform`/effect params structured so keyframe
  tracks can attach later without model rewrite).

## Program roadmap

- **Phase 1 — Cut (this doc)**: NLE core + Premiere-grade shell.
- **Phase 2 — Motion (After Effects)**: keyframe tracks on
  transform/opacity/effect params with interpolation + graph editor,
  masks on clips (`MaskSystem` exists), shape/text layers, comps =
  nested timelines, render queue.
- **Phase 3 — Photo (Photoshop)**: still-image mode — layer stack,
  blend modes (`MixSystem`), adjustment layers, filter gallery,
  masks — reusing the same layer/composite infrastructure.

Phase-1 model note for Phase 2: keep per-clip params (transform,
effect params, opacity) in a structure that can later carry
`keyframes[]` tracks without breaking JSON v1 compat (version the
format).

## What's left to the builder

- Exact file/type names inside `tools/video_editor/` (layout above is
  a guide, not a contract)
- Curated effect list + parameter schemas (within `VideoEffects`)
- Wipe/slide pixel implementation detail (mask vs offset lerp)
- Software scale/rotate backend choice (Skia vs custom)
- Track add/remove/rename UI (minimal: add-track + delete-empty-track)
- Project settings dialog placement (canvas w/h/fps/bg editing)
- Whether to extend `CompositingEngine::Layer` or keep transform inside
  the editor's own compositor

## Builder calls already made (flag if you disagree)

- Suite architecture: ONE app with workspace switcher (Cut now, Motion
  + Photo later) — not three binaries. See
  `docs/wiki/architecture/suite-architecture.md`.
- Project extension `.xerproj`; catalog at
  `%LOCALAPPDATA%/XER/VideoEditor/catalog.db`
- Preview engine pools `VideoEngine` per clip (not `VideoSession` —
  audio attach lives in the facade)
- Export decoders are per-clip, not per-path
- Headless CLI `video_editor --export in.xerproj out.mp4` is **in
  scope** — `export_engine` is UI-free anyway, and it doubles as the
  automated test path
- Transition UI: "add transition" action on a cut (context menu /
  inspector), not drag-onto-edge gestures
- Transition model: centered window reading handle frames from source
  media beyond in/out points (standard NLE)
- `clip.muted` = audio only; video tracks use `hidden`; audio tracks
  use `muted`
- Phase-1 tool strip: selection/razor/hand/zoom/rate-stretch;
  slip/slide/roll edits stay Phase 2
- JSON lib: pick jsoncpp or nlohmann at implementation time
