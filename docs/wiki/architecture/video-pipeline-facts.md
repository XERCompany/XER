# Video pipeline — verified facts

Verified by direct source reads + builder-check subagent, 2026-11-15.
Repo: `D:\Developers\XER3`. All paths relative to repo root.

> **2026-11-15 update (Phase-1A, branch `feature/xer-studio-phase1`,
> commits `6742124..a0f5058`, unmerged):** several claims below are
> superseded — see "Phase-1A additions" at the bottom. Original claims
> kept for history.
>
> **2026-11-15 update (Phase-1B, branch
> `feature/xer-studio-1b-timeline`, commits `a0f5058..5489535`,
> unmerged):** ProbeVideo accepts audio-only, `EnableAudioTap` Dummy
> fixed, new `XER_VideoEditor_Core` lib + `XER_VideoEditor_Tests` — see
> "Phase-1B additions" at the bottom.

## Platform gates

- FFmpeg bridge is **Windows/MSVC-only**: `ENGINE_HAS_FFMPEG_BRIDGE`
  etc. defined only `if(MSVC)` in `src/wrapper/ffmpeg/CMakeLists.txt:45-53`;
  `ffmpeg_video_decoder.cc` is entirely `#ifdef _WIN32`.
- `VideoAudioPlayer` is WASAPI/`_WIN32` (`src/content/video/`).
- Consequence: off-Windows the whole video path is stubs; a `--info`
  CLI that probes media is vacuous there (builder-check finding).

## Orphan lib

- `XER_Video` (`src/video/`): `add_library` at `src/video/CMakeLists.txt:15`,
  `add_subdirectory(src/video)` unconditional at root `CMakeLists.txt:159`.
  ~~**zero binaries link it**~~ — SUPERSEDED 2026-11-15: `XER_Video_Smoke`
  (`tests/video/xer_video_smoke.cc`) is the first consumer (orphan-lib
  milestone for Phase-1 program). Contains FrameExporter,
  ClipFrameCache, VideoManager, CompositingEngine, MixSystem, MaskSystem,
  FragmentCutter, VideoEffects, renderers (CUDA/Vulkan/ANGLE/Skia).
- `XER_Video` PUBLIC-links `XER_FFmpeg XER_AudioCore XER_Cache XER_Helper
  XER_Flux_Core` + optional bridges; **references Skia symbols
  unconditionally** — won't link without the Skia port.

## FFmpeg bridge (`src/wrapper/ffmpeg/`)

- `ffmpeg_engine_bridge.h`: `ProbeMedia`, `ReadPackets`,
  `DecodeVideoFrames` (batch→native fmt), `DecodeAudioFrames`
  (batch→PCM, **no seek**, whole vector), `RemuxCopy`, `ExtractStream`,
  `EncodeVideoFrames`, **`EncodeVideoFrameStream`** (streaming,
  accepts BGRA via sws), **`EncodeAudioFrames`** (batch audio-only),
  `ImageToJpeg` (**first frame only**), `RunFilterGraph`/`Transcode`/
  `CaptureDeviceToStream` = **MOCKS**.
- ~~**No A/V mux**~~ — SUPERSEDED 2026-11-15 (`a0f5058`):
  `EncodeAudioVideoStream` added; see "Phase-1A additions" below.
- `ffmpeg_video_decoder.h` `VideoDecoder` (streaming): `EnableAudioTap`,
  `Open/Close/IsOpen`, `NextFrame→packed BGRA`, `Seek` (keyframe+flush
  ONLY — **no forward discard**), `GetPositionSeconds` (PTS),
  `HasAudio/GetFps/GetDurationSeconds/GetWidth/GetHeight`,
  `GetAudioStartSeconds/GetAudioTimeBase`, `ReadAudioFrames`,
  `AudioDrained`, `GetAudioEpoch`.
- ~~**`Open` rejects audio-only files**~~ — SUPERSEDED 2026-11-15
  (`4d1cecd`+`1e2bd63`): `Open` accepts audio-only containers; see
  "Phase-1A additions" below. ~~NOTE: `ProbeVideo`/`VideoSession` still
  gate audio-only out of the preview path~~ — SUPERSEDED 2026-11-15
  (Phase-1B, `5489535`): `ProbeVideo` accepts audio-only (w/h=0) and
  `VideoSession` proceeds with width 0; see "Phase-1B additions".
- **Audio tap starvation**: audio packets queue only while `NextFrame()`
  pumps the demuxer; queue ≤2048 packets, drops oldest on overflow.
  For A/V files audio export must pump video decode concurrently; for
  audio-only files `NextFrame()` itself is the pump (bounded 256
  packets/call, returns false).
- Seek precision: bridge Seek = keyframe-backward + flush. The ≤120
  frame forward-discard (`kSeekMaxDiscardFrames`) lives in the CONTENT
  layer `VideoSession::ApplySeek` (video_session.cc:15,282-296) with
  ~0.1s slop — preview seeks aren't frame-accurate either.

## Content layer (`src/content/video/`)

- `VideoEngine` facade: `Open/Close/Play/Pause/Stop/Seek/SetLoop/Tick`,
  `GetLatestFrame/GetFrameSequence`, `RecallFrame/GetCachedPoster`,
  `HasAudio/AudioReady`. Internally: `VideoSession` (decode worker +
  single-slot frame mailbox) + `VideoCommandBuffer` +
  `VideoAudioPlayer` (WASAPI, attaches to session's decoder) +
  `VideoFrameCache` (memory + `.xervid` poster).
- `VideoSession::GetAvDecoder()` exposes the shared Bridge decoder
  (audio tap already enabled, video_session.cc:195).
- `VideoDecoder` (Content adapter) wraps Bridge decoder; audio API NOT
  re-exported.
- `ProbeVideo(path, VideoInfo*, err)` → width/height/fps/duration/
  codecs/has_audio; audio-only files probe OK with w/h=0.
- `VideoFrame` = packed BGRA + position_seconds + sequence.

## src/video (`XER_Video`)

- `video::Frame`: packed BGRA8, `PixelFormat{BGRA8,RGBA8,NV12,YUV420P}`,
  meta{width,height,pts,is_keyframe}.
- `CompositingEngine::Layer` = {frame, z_order, x_offset, y_offset,
  opacity} — **no scale/rotation**.
- `MixSystem::Blend` is an **instance method** (not static).
- `VideoEffects`: BrightnessContrast, Grayscale, BoxBlur, GaussianBlur,
  Sharpen, EdgeDetect, Emboss, Vignette, ChromaticAberration, Pixelate,
  OilPaint, MotionBlur, FlipH/V, GrayscaleWithMask. **Several are
  Skia-only with NO CPU fallback** (Sharpen/Vignette/Pixelate/EdgeDetect/
  Emboss/ChromaticAberration/OilPaint/MotionBlur silently no-op when
  `XER::Bridge::Skia::IsAvailable()` is false — video_effects.cc:111-186).
- `MaskSystem`, `MaskPointsVector`, `FragmentCutter` (mask bbox cut),
  `ClipFrameCache` (disk frame cache `.dxcfz`), `ClipGpuPostprocess`,
  `music_bass_eq_clip.cc` = reference for render→cache→ExportFrameStream
  → TempPathForTarget/CommitTempFile atomic write (lines 570-586).
- `FrameExporter`: `ToFile` (PNG), `ExportFrames`,
  `ExportFrameStream(path, count, w, h, codec, fps, provider)`.
- `VideoManager`/`VideoStream`/`FFmpegFrameSource`: multi-stream
  compositing orchestrator (OpenUri etc.) — heavier than needed for
  export; editor's own compositor is thinner.

## UI / tools

- Widget kit `src/ui` (retained mode, Win32+D2D+DWrite): Widget virtuals
  `Render/Render/Layout/HandleEvent/Tick`; `Renderer::DrawFrameBgra`.
- `tools/preview` = template: root-CMakeLists `add_executable`,
  `XER_UI_Core` link, `#if defined(_WIN32)` GUI + CLI fallback,
  `Application::Instance()/Window::Create/SetContent/Run`.
- `VideoPreviewWidget` = reference for hosting `VideoEngine`.
- `node_graph_history.h`: Command{Apply/Revert/Label/MergeWith} +
  History{Execute/Undo/Redo/BeginMacro/EndMacro} — undo pattern.
- JSON: repo uses both jsoncpp and nlohmann_json — CMake sketch needs
  one added.
- SQL: `XER::SQL::Storage::SQLiteDatabase` raw SQL
  (Execute/Query/prepare-bind-step).
- Tests pattern: `tests/ui/ffmpeg_video_decode_tests.cc` (encode→decode
  roundtrip); plain-assert style. Phase-1A added
  `tests/ui/ffmpeg_audio_tap_tests.cc` + `ffmpeg_av_mux_tests.cc`
  (XER_UI_TEST harness) and `tests/video/xer_video_smoke.cc`
  (plain-assert main, MSVC-gated in tests/CMakeLists.txt).

## Phase-1A additions (as of `a0f5058`, 2026-11-15, unmerged branch)

- **`EncodeAudioVideoStream`** (`ffmpeg_engine_bridge.h:237-258`):
  `EncodeAudioVideoParams{video: EncodeVideoParams, audio:
  EncodeAudioParams}`, `AudioFrameStreamProvider`, interleaved two-stream
  mux into ONE mp4. `video.output_path` authoritative; `audio_frame_count
  ==0` → video-only file; `video_frame_count==0` → error. Interleave by
  cumulative media time (video ≈ frame_index/fps, audio = Σ
  sample_count/sample_rate per chunk, ties→video); both encoders flushed
  before trailer; manual-cleanup single epilogue.
- **Encoder internals** (`ffmpeg_engine_bridge.cc` anonymous namespace,
  ~L806-1368): `VideoEncodeState`/`AudioEncodeState` +
  `Setup*/Feed*/Flush*Encoder*`/`OpenOutputAndWriteHeader`/
  `Release*EncodeState` — Task-4 mux composes these; future encode work
  should too. Header unchanged.
- **`VideoDecoder` audio-only support** (`ffmpeg_video_decoder.{h,cc}`):
  `Open` accepts audio-only containers (`IsOpen` = fmt &&
  (codec || audio_stream)); audio-only `NextFrame()` pumps ≤256
  packets/call into tap, returns false; consumer contract = alternate
  `NextFrame()`+`ReadAudioFrames()` until `AudioDrained()` (documented in
  header). `ReleaseAll` resets width/height/fps/position; every failed
  `Open` path calls `Close()`; `demux_read_errors>=3` → demux EOF.
  GetWidth/Height=0, GetFps=30.0, GetPositionSeconds=-1.0 on audio-only.
- **Known issues FIX-SOON for phases 1B-1C** (from final review):
  - `VideoDecoder::Seek` fallback passes `AV_TIME_BASE` ts to
    stream-indexed `av_seek_frame` — needs `av_rescale_q` (wrong position
    on audio-only seeks; ~1 line).
  - Skia `BlendSrcOver` (`skia_engine_bridge.cc:546-563`) blends in
    premultiplied space w/o un-premultiply + `static_cast<uint8_t>`
    wraps >255 — `MixMode::Alpha` partial-alpha blends wrong on ALL
    Windows builds; breaks 1C transitions. Smoke test codifies current
    (buggy) output — will flip red when fixed (feature).
  - ~~`ProbeVideo` still rejects audio-only ("no video stream")~~ —
    FIXED in Phase-1B (`5489535`): accepts audio-only, w/h=0.
  - `position_seconds` never advances on audio-only playback — 1C should
    read `AudioFrameInfo.pts` instead; needed for progress reporting.
  - Failed mux leaves partial file at output_path — atomic temp→commit
    is export_engine's job (spec), add a line to 1C plan.
  - ~~`EnableAudioTap` unguarded `impl_->audio_tap` won't compile on
    non-Windows (`Impl`=Dummy)~~ — FIXED in Phase-1B (`5489535`,
    ffmpeg_video_decoder.cc:256-271).
  - `params.video.pixel_format` accepted but ignored by encoders
    (pre-existing); `FlushAudioEncoderFifo` derefs
    `codec_context->codec` w/o null check (safe under Setup-first
    contract).

## Phase-1B additions (as of `5489535`, 2026-11-15, unmerged branch
`feature/xer-studio-1b-timeline`)

New headless core `XER_VideoEditor_Core` (`tools/video_editor/`,
static lib, C++23, no exe consumer yet — 1C adds the `video_editor`
exe; only consumer today is `XER_VideoEditor_Tests`,
`tests/video/`, plain-assert harness in `video_editor_test_util.h`,
five suites: model/history/store/poster/catalog, registered in ctest
unconditionally — FFmpeg deps degrade to stubs off-Windows).

- **`timeline_model.{h,cc}`** — pure-data `Project{width,height,fps,
  bg_color,tracks,clips,transitions}`; `TrackKind{Video,Audio}`;
  `Clip{id,source_path,track,timeline_start,in_point,duration,muted,
  gain_db,fades,transform,effects}`; `Transition{from_clip,to_clip,
  Type,Crossfade|Wipe*|Slide*,duration}`. All mutating ops are
  validate-then-mutate and transactional. Invariants: `clip.track`
  in range; audio-only media (`MediaBounds::has_video==false`) only on
  Audio tracks; unique non-empty ids; no same-track overlap (adjacency
  OK); transitions only between adjacent clips on the same VIDEO
  track. `DropInvalidTransitions` runs after every mutation and on
  store load. Queries incl. `CollectSnapPoints`/`SnapTime` (snap =
  query helpers, NOT a history command — spec deviation).
- **`timeline_history.{h,cc}`** — `Command{Apply/Revert/Label/
  MergeWith}` + `TimelineHistory{Execute/Undo/Redo/BeginMacro/
  EndMacro}` mirroring `XER::NodeGraph::History`. Clip-edit commands
  capture touched clips by vector index + a FULL `transitions` copy
  before first Apply and restore both on Revert (a move/trim/split
  that made the model drop transitions brings them back verbatim);
  track commands snapshot the whole tracks vector. `f2c1df1` fixed a
  merged-command redo divergence on transition changes.
- **`project_store.{h,cc}`** — `.xerproj` JSON v1 (`kVersion=1`),
  jsoncpp. Load is strict for primary data (required fields, types,
  duplicate ids, out-of-range track indices, negative times) but
  self-heals ONLY derived data: transitions with missing endpoints,
  non-positive/non-finite duration, non-video track, cross-track, or
  non-adjacent endpoints are dropped (`c346606` also rejects
  overlapping clips on load as an error). Save is crash-safe:
  `<path>.tmp` then rename (remove+retry on Windows).
- **`clip_poster.{h,cc}`** — `ClipPoster::Extract(src, seek_s,
  out_png)`: `VideoDecoder` seek (keyframe-backward + bounded forward
  decode, last-frame fallback at EOF) → `NextFrame` →
  `FrameExporter::ToFile`. `seek_seconds` clamped to [0,duration];
  rejects audio-only (w/h==0); creates parent dirs. Posters are **PNG,
  not .jpg** (spec deviation — `FrameExporter` has no JPEG path).
- **`media_catalog.{h,cc}`** — SQLite catalog via
  `XER::SQL::Storage::SQLiteDatabase`, one connection, UI-thread-owned
  (callers serialize). `CatalogEntry{id,path,duration,w/h,fps,
  video_codec,audio_codec,has_audio,file_size,mtime,probe_json,
  poster_path,added_at}`. `Import` = `ProbeVideo` + upsert by
  canonical path; `extract_poster` && w>0 → `ClipPoster::Extract` at
  `duration*0.5` into `<db_dir>/posters/<id>.png` (poster failure is
  NON-fatal). `Remove` deletes row + best-effort poster file.
  `DefaultDbPath()` env chain: `LOCALAPPDATA` → `XDG_DATA_HOME` →
  `$HOME/.local/share` → temp dir → `"."`, then
  `/XER/VideoEditor/catalog.db`.
- **`video_info.cc`** (`5489535`) — `ProbeVideo` accepts audio-only
  containers: succeeds with w/h=0, `has_audio` set, duration falls
  back to the audio stream when container/ video duration absent.
- **`ffmpeg_video_decoder.cc`** (`5489535`) — `EnableAudioTap` guarded
  so it compiles against the non-Windows `Impl=Dummy`.
- **Remaining handoffs to Phase 1C** (unchanged from 1A list):
  Skia `BlendSrcOver` premultiply/wrap bug; `VideoDecoder::Seek`
  fallback needs `av_rescale_q` for stream timebase; audio-only
  `position_seconds` stays -1.0 (read `AudioFrameInfo.pts` for
  progress); export mux must do atomic temp→commit itself.

### Phase-1B spec deviations (recorded)

- Poster files are `.png` (`posters/<id>.png`), not `.jpg` —
  `FrameExporter::ToFile` only writes PNG.
- `Transform.offset_{x,y}` = clip top-left as a fraction of canvas
  w/h (not center-based).
- Snapping is query helpers `CollectSnapPoints`/`SnapTime`, not a
  history/undo command.
- `RemoveTrack` reindexes `clip.track` for all clips on later tracks
  (required because tracks are index-based).
- `.xerproj` load self-heals invalid transitions only; overlapping
  clips / bad primary data are hard errors.

### Phase-1B final-review hardening (`b966af9`, 2026-11-15, unmerged)

- `timeline_model` mutators now reject non-finite (`NaN`/`±Inf`) on ALL
  double inputs — timing, gain/fades, transform, effect params —
  via `ValidateClipPlacement` + `TrimClip`/`SplitClip`/`AddTransition`
  guards. `SplitClip` validates the second half (transactional).
- `ProjectStore::Load` wraps `Json::parseFromStream` in try/catch —
  never throws (deep-nesting parser overflow → clean error).
- `ProjectStore::Load` ALSO rejects overlapping clips on load (hard
  error) and drops transitions with `duration <= 0`/non-finite
  (self-heal) — load enforces the same invariants mutators do.
- **`VideoSession` loop guard**: loop-restart requires
  `decoder->GetWidth() > 0` AND <3 consecutive fruitless restarts —
  audio-only + loop no longer busy-spins (reaches kEnded). Video
  looping unaffected (verified: width>0 always true for video files,
  counter resets on each published frame).
- **Contract**: loading a `.xerproj` into a `Project` with a bound
  `TimelineHistory` requires `history.Clear()` first — documented on
  both APIs; undo history is not migrated across loads.
- Latent (deferred): `ProjectFromJson` doesn't isfinite-check
  presentation fields — safe only because jsoncpp 1.9.6 rejects
  overflow literals; revisit if jsoncpp is upgraded.

### Phase-1C Task 1+2 (`6484218`, `a1c18ee`, `ad42397`, 2026-11-15)

- **`VideoDecoder::Seek` fallback FIXED** — `av_rescale_q(ts,
  AV_TIME_BASE_Q, fs->time_base)` before stream-indexed `av_seek_frame`
  (`ffmpeg_video_decoder.cc:544-588`). Primary `avformat_seek_file`
  path unchanged. KNOWN ISSUE: fallback branch unreachable via stock
  fixtures (avformat_seek_file returns 0 for wav/m4a/mp4) — covered
  end-to-end only; exotic-container coverage would need a test seam.
- **Skia `BlendSrcOver` FIXED** — straight-alpha src-over
  (`oa = sa + da(1−sa)`, `oc = (sc·sa + dc·da(1−sa))/oa`) at
  `skia_engine_bridge.cc:546-569`, float math + clamp, matches
  `BlendSrcOver_ps`. Consumer audit: all ACTIVE consumers pass
  straight-alpha 0xRRGGBBAA; no active path relied on the bug.
  SUPERSEDES the 1A/1B "FIX-SOON" item for BlendSrcOver.
- **NEW latent hazard — `DecodeImage` premultiplied output**
  (`skia_engine_bridge.cc:1246-1284`): `SkCodec` decodes with
  `kPremul_SkAlphaType` and only byte-swizzles → premultiplied pixels
  under a straight-alpha contract. Was accidentally correct under the
  OLD buggy BlendSrcOver; now produces too-dark composites. ZERO
  current callers — must be fixed (unpremultiply; dormant
  `UnpremultiplyAlpha` exists ~L513) BEFORE any PNG/image-overlay
  feature lands (Phase 2+).
- **`IMediaDecoder` abstraction** (`tools/video_editor/media_decoder.*`)
  — 18-virtual interface mirroring `VideoDecoder` contract exactly +
  `BridgeVideoDecoder` pass-through adapter + `MediaDecoderFactory`
  for fake-decoder tests. Single-consumer-thread documented.
  Contract nuances for Tasks 4-6: Seek presets position to CLAMPED
  target (no clamp when GetDurationSeconds()==0); audio accessors
  inert without EnableAudioTap-before-Open (AudioDrained may never
  become true → never drain-loop untapped); GetAudioEpoch bumps on
  Seek ONLY (not Close/Open); ReadAudioFrames null `out` → false.
- **Decoder quirks on encoder-produced mp4s** (verified container
  artifacts, not adapter bugs): `GetFps()` reports `avg_frame_rate`
  (27.43 on an 8-frame/24fps fixture — nb_frames/stream-duration
  mismatch), `GetDurationSeconds()` returns `fmt->duration` = 0.0.
  Downstream code must NOT trust these for per-clip duration — use
  `MediaBounds`/probe metadata instead. A/V-muxed fixtures can yield
  N−1 decodable frames (7 of 8) via BOTH batch and streaming decode.
- **`AudioDrained()` can flip true while video B-frames remain
  buffered** — consumers must keep calling `NextFrame()` to false
  independently; drain loops must run until `video_done &&
  AudioDrained` (pattern in decoder_tests.cc case 3a).
- Still open for 1C/1D: audio-only `position_seconds` stays -1.0
  (use `AudioFrameInfo.pts * GetAudioTimeBase()`); atomic
  temp→commit is export_engine's job; track/project-settings model
  mutators still missing (1D inspector scope).

### Phase-1C Task 4 additions (compositor, `3eb5249`+`1744bb4`, 2026-11-15)

- **Compositor contract**: per-clip lazy `IMediaDecoder` sessions on
  VIDEO tracks only; Seek once to `max(0, src_t-0.25)` best-effort;
  sequential forward decode, 3-slot ring, nearest-PTS ties→prev,
  EOF freeze, synthesized pts for timestamp-less streams (fps-seeded),
  `kMaxDecodeSteps=1e6` cap. CPU BGRA8 only; effects via
  `video::VideoEffects` (14 types; Skia-only gated on IsAvailable).
- **NEW decoder limitation (Task-4 review minor #3)**: for
  timestamp-less streams the bridge seeds synthesized positions from
  the SEEK REQUEST, not the landed keyframe → pts carry constant
  (seek_target − keyframe) offset up to GOP length. Frame-accurate
  selection can be off on no-PTS+long-GOP files. Task 6 export should
  not rely on perfect accuracy there.
- Compositor treats `GetFps()`/`GetDurationSeconds()` as unreliable
  (encoder-mp4 quirks) — duration comes from project/MediaBounds.

## Media classification rule (2026-11-20, agent 46304831)

Any imported file resolves to exactly one of:
- **Video stream w/h>0** → `has_audio` per stream presence → **Video track**
- **No video stream, audio present** → **Audio track**
- **Neither** → import rejected ("no decodable audio/video streams")

Supporting rules (media_catalog.h holds the authoritative doc):
- Stills = video: OIIO fallback for ANY probe-failed file (decode is
  the accept test, not an extension whitelist); probed ≤0.25 s
  no-audio files normalize to 5 s still duration.
- Cover art is NOT video: `ProbeVideo` skips attached-picture streams
  (AV_DISPOSITION_ATTACHED_PIC / single-frame image codec next to
  audio) so tagged mp3/flac/m4a classify as audio.
- 2026-11-21 hardening (cover art that dodged the first heuristic):
  `StreamInfo::attached_pic` now carries the disposition flag, and
  `IsAttachedPictureStream` (exported from video_info.h as the test
  seam) additionally treats ANY picture stream in an audio-only
  demuxer (mp3/flac/wav-family — tokenized from
  MediaInfo::format_name; mov/mp4/m4a, matroska/webm, ogg, asf stay
  general) as art, plus 1-2 frame image-codec streams next to audio
  in general containers. Unknown (0) frame counts are NOT condemned
  in general containers — camera MJPEG footage stays video.
- Timeline flicker fix (same change): `ClipPoster::GetPosterAt`
  area-downscales posters to <= 320 px on the long edge — full-res
  BGRA frames (~8.3 MB @1080p) overflowed the 64 MiB PosterCache in
  ~7 entries vs dozens of visible 0.25 s buckets per clip, and the
  LRU thrash was what made thumbnails blink.
- `MediaBounds.has_audio` does not exist — placement only gates on
  `has_video`; the mixer taps audio from the source file regardless
  of track kind.
- Import dialog: ~80 curated extensions + `*.*` second filter.

## Audio from video clips — silence root cause + multichannel (2026-11-20, agent 0664d783)

- **Root cause of "no sound from video files"**: `WasapiSink::Initialize`
  only accepted s16/f32 stereo AT the requested rate; shared-mode
  endpoints with a different mix rate (44.1 kHz onboard, BT headsets,
  multichannel endpoints, pre-1709 Windows) returned
  FORMAT_NOT_SUPPORTED → null sink → silent preview while the mixer
  produced PCM the whole time. Fixed: three-tier negotiation —
  direct format, `AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM` engine SRC, or
  adopt `IsFormatSupported` closest-match + client-side convert
  (linear resample + channel remap + s16/s24/s32/f32).
- `AudioFrameInfo` gained `channel_layout` (AV_CHANNEL_LAYOUT mask,
  0 = unknown); populated in both `CollectAudioFrame` and
  `CollectTapAudioFrame`.
- Mixer downmix is layout-aware (ITU-R BS.775-style: FC −3 dB,
  surrounds −6 dB, LFE dropped); count heuristic only when mask is
  zero/mismatched; >8ch warns once and uses first-two fallback.
  Supports 1/2/3.0/quad/4.1/5.1(side+rear)/6.1/7.0/7.1/7.1-wide,
  interleaved + planar.
- `IAudioSink::LastError()` added; sink rejection warnings carry it.
- `platform_audio_sink_` no longer latches null forever —
  `EnsurePlatformAudioSink()` retries throttled (3 s) so late
  devices recover; first failure reports once.

## Montage silence — actual root cause (2026-11-20, agent 8be92390)

- `WasapiSink::Open` called `CoInitializeEx(COINIT_MULTITHREADED)` and
  treated failure as fatal; the UI thread is already STA (D2D/canvas)
  → `RPC_E_CHANGED_MODE` → `MakePlatformAudioSink` null → audio_sink
  nullptr forever. Headless tests used fake sinks so never caught it.
  Fixed: `RPC_E_CHANGED_MODE` normalized to success (WASAPI render
  objects are free-threaded; `com_init_` stays false so `Close()`
  doesn't pop an init it doesn't own).
- Shared `platform_audio_sink_` between preview + source engines was
  also racy (two worker threads → one IAudioRenderClient). Now
  `program_audio_sink_` + `source_audio_sink_` — one stream each.
- **Linked pair**: A/V import → video clip + audio clip sharing
  `link_group` (model already had it). Mixer suppresses embedded
  audio of a video clip whose link_group partner sits on an audio
  track (no double-decode); deleting the audio leg restores it.
- Earlier WASAPI negotiation fix (AUTOCONVERTPCM/closest-match) and
  layout downmix remain valid — this was a second, earlier blocker.
