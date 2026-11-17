# Phase 1C — Compositor + Export Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use subagent-driven-development or executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The frame-accurate offline render path of the VideoEditor —
decode → clip effects → affine transform → track stack → transitions →
canvas BGRA (`Compositor`), mixed PCM audio (`AudioMixer`), single-file
A/V mux with atomic temp→commit write, progress + cancel
(`ExportEngine`), and a headless `video_editor --export in.xerproj
out.mp4` CLI. All UI-free; lands inside `XER_VideoEditor_Core` plus one
new exe.
**Architecture:** Three new modules in `XER::VideoEditor` under
`tools/video_editor/`: (1) `media_decoder` — `IMediaDecoder`
abstraction over `XER::Bridge::FFmpeg::VideoDecoder` so compositor/
mixer tests inject synthetic decoders (no FFmpeg needed for the pure
composition logic); (2) `compositor` — owns a `Project` snapshot, one
lazily-opened decoder per clip, sequential forward decode with
nearest-PTS frame selection, CPU effect chain, own affine warp
(offset/scale/rotation/opacity), per-track layer composite, transition
geometry (crossfade/wipe/slide) — all in packed BGRA8; (3)
`audio_mixer` — per-clip audio-tap decoders → stereo float window
indexed by media time → gain/fade/transition-weighted mix → s16 chunks
for the mux provider; (4) `export_engine` — worker thread driving
`EncodeAudioVideoStream` / `EncodeVideoFrameStream` /
`EncodeAudioStream` (new streaming audio-only API, Task 3) by media
presence, `TempPathForTarget` + `CommitTempFile` atomic write. Two
shared-engine fixes ride along:
`VideoDecoder::Seek` fallback `av_rescale_q` and Skia `BlendSrcOver`
straight-alpha math. `main.cc` adds `--export`/`--info`; the
`video_editor` exe links `XER_VideoEditor_Core` only (no `XER_UI_Core`
until 1D).
**Tech Stack:** C++23, CMake + Ninja (preset `default`, vcpkg
`x64-windows-release`, MSVC), FFmpeg bridge headers only (never libav*
directly), `video::Frame`/`VideoEffects` from `XER_Video`,
`XER::Cache` temp/commit, plain-assert test style.
**Spec:** docs/superpowers/specs/2026-11-15-video-editor-design.md
(export pipeline ~L319-416, data model ~L156-267, preview ~L289-317);
plan index 1C block:
docs/superpowers/plans/2026-11-15-phase1-plan-index.md
**Baseline:** Phase-1B merged to main at `b966af9` (1B progress doc:
`docs/wiki/progress/phase-1b-timeline-core.md`; verified facts incl.
Phase-1A/1B handoffs:
`docs/wiki/architecture/video-pipeline-facts.md`).

## Global Constraints

- **MSVC env required for every cmake/ctest invocation** — plain shells
  fail `C1083` on standard headers. Use the repo helper:
  `"D:/Developers/XER3/out/vcenv.bat" cmake --build out/build/default --target <t>`
  and `"D:/Developers/XER3/out/vcenv.bat" ctest --test-dir out/build/default --output-on-failure`.
  (Re-configure after CMake edits: `"D:/Developers/XER3/out/vcenv.bat" cmake --preset default`.)
- Build dir `out/build/default` (Ninja preset `default`); test/tool exes
  land in `out/build/default/bin` via the global
  `CMAKE_RUNTIME_OUTPUT_DIRECTORY`.
- **C++23**, repo style for NEW files in `tools/video_editor/`: the
  existing 1B files use **4-space indent** (not tabs — match the
  directory, not the wider repo), snake_case files, camelCase/PascalCase
  code, `#pragma once`, RAII, no exceptions in hot paths, contextual
  errors via `std::string* error_out` (nullptr-tolerant). `/W4` clean.
- **Namespace `XER::VideoEditor`** for all new code; sources live flat
  in `tools/video_editor/` and are auto-globbed into
  `XER_VideoEditor_Core` (`file(GLOB CONFIGURE_DEPENDS *.cc)` in
  `tools/video_editor/CMakeLists.txt:9-11`) — **`main.cc` must be
  excluded from that glob** (Task 7) or `main()` lands in the static
  lib.
- **No FFmpeg/libav headers outside `src/wrapper/ffmpeg`** — editor
  code includes only `wrapper/ffmpeg/ffmpeg_video_decoder.h` /
  `ffmpeg_engine_bridge.h` (and `media_decoder.h` forward-declares
  `AudioFrameInfo`). `AV_NOPTS_VALUE` is `INT64_MIN` — compare against
  `std::numeric_limits<std::int64_t>::min()` with a comment; do NOT
  include `<libavutil/...>` for it. Tests synthesize media at runtime
  in `%TEMP%` (repo pattern) and self-skip via
  `XER::Bridge::FFmpeg::IsAvailable()`.
- **`video::Frame` is packed BGRA8** (`frame.h`): byte order B,G,R,A,
  `Stride() == width*4`, `Allocate` zero-fills. Decoded `NextFrame`
  output is the same layout — wrap by `memcpy` into a `Frame` (the
  `clip_poster.cc:104-109` pattern). `Frame::Allocate` re-zeroes on
  every call — reuse scratch `Frame`s and overwrite `Data()` when
  dimensions are unchanged (hot-path rule).
- **Pixel math is straight-alpha BGRA** everywhere in the compositor —
  NOT premultiplied. `bg_color` is interpreted as **{r,g,b,a}**
  (resolved ambiguity — the store round-trips the raw array; only the
  compositor interprets it; symmetric defaults like {0,0,0,255} are
  unaffected either way).
- **Timeline model is UI-thread-owned, synchronous, pure** — the
  compositor/mixer/export engine each take a **copy** of the `Project`
  at `Init`/`Start`; later edits never affect a running export.
  `ProjectStore::Load` + `TimelineHistory` contract stands: CLI loads
  with no history bound (trivially satisfied); document it in `main.cc`.
- **No GPU renderers** — export renders CPU BGRA only; do NOT use
  `CompositingEngine`, `VideoManager`, CUDA/Vulkan/ANGLE paths, or
  realtime capture. Skia *filters* inside `VideoEffects` are allowed
  (CPU raster, already linked via `XER_Video`) but the compositor's own
  pixel loops must not depend on Skia.
- **`XER_VideoEditor_Core` is the ONLY new lib** — compositor/mixer/
  export_engine are UI-free and join it via the existing glob; the
  `video_editor` exe is the only new target. No root `CMakeLists.txt`
  change needed (`add_subdirectory(tools/video_editor)` already at
  `CMakeLists.txt:463`).
- Do **not** touch mock bridge functions (`RunFilterGraph`, `Transcode`,
  `CaptureDeviceToStream`), `src/content/video` session/engine code, or
  add model mutators (see "Spec gaps resolved" — track/project
  settings are read-only in 1C).

## Review Focus

The five input classes / failure modes most likely to bite:

1. **A/V sync drift**: video frame `n` renders at `t = n*fps_den/fps_num`
   while audio chunk `m` covers `[m*chunk/rate, ...)` — the mux
   interleaves by *cumulative provider durations*, so any mismatch
   between `project.duration()`, `total_frames`, and
   `audio_chunk_count*chunk_samples/rate` shifts the tail. Per-clip
   audio must anchor to `AudioFrameInfo.pts * GetAudioTimeBase()`
   (audio-only `GetPositionSeconds()` stays -1.0), handle AAC
   priming/`GetAudioStartSeconds()`, and keep chunk formats
   consistent (the encoder fixes input format from chunk 0 and rejects
   any deviation — `ffmpeg_engine_bridge.cc:1192-1198`).
2. **Transition frame math at boundaries**: the window is
   `[cut - D/2, cut + D/2)` with `w = (t - (cut-D/2))/D`; at `w=0`
   output must equal the `from` layer, at `w=1` the `to` layer, and at
   the exact cut `w=0.5`. Handle regions extend `src_t` past
   `in_point`/`out_point` — source time clamps to `[0, media_duration]`
   ("edge freezes" for video = repeat last decoded frame; audio =
   silence, never fabricated PCM). Off-by-one at window edges,
   non-adjacent/overlapping transition pairs, and `duration/2` longer
   than a clip (3-candidate degenerate case) must be pinned by tests.
3. **Decoder starvation on sparse/missing PTS**: `Seek` is
   keyframe-backward only; forward-discard must tolerate VFR, large
   GOPs, missing timestamps (`GetPositionSeconds` falls back to a
   `+1/fps` estimate internally — the compositor must not assume
   monotonic-perfect pts), and EOF-before-target (freeze on last
   frame). Audio tap packets only flow while `NextFrame()` pumps —
   audio-only files return false from `NextFrame` yet still pump
   (bounded 256 packets/call); A/V files pay a full video decode per
   pump. A clip whose decode produces nothing must error, not spin.
4. **Cancellation during export and cleanup**: `Cancel()` must work
   while blocked inside decode, audio pumping, render, or an encoder
   provider callback; the worker must delete the temp file and NEVER
   commit on cancel/failure, `CommitTempFile` failure must not
   overwrite-target-loss, and `~ExportEngine` must join the thread
   (no detached worker touching a destroyed engine).
5. **Memory blowup on long exports**: no full-timeline PCM or frame
   buffers — video streams one canvas frame through the provider,
   audio streams fixed-size chunks; per-clip decoder sessions close
   after their last contributing frame; the audio PCM window is
   bounded by a media-time margin, not by clip duration. A 30-minute
   project must not allocate duration-proportional RAM.

---

## Task 1: Engine fixes — `VideoDecoder::Seek` rescale + Skia `BlendSrcOver`

Two verified handoff bugs from Phase-1A (`video-pipeline-facts.md`
"Known issues FIX-SOON"). Both are small, shared-engine fixes with
regression tests; they land BEFORE editor code so 1C builds on correct
primitives. Independently committable.

**Files:**
- Modify: `src/wrapper/ffmpeg/ffmpeg_video_decoder.cc` (`Seek` fallback,
  ~lines 553-566)
- Modify: `src/wrapper/skia/skia_engine_bridge.cc` (`BlendSrcOver`,
  ~lines 546-563)
- Modify: `tests/ui/ffmpeg_audio_tap_tests.cc` (add seek-position case)
- Modify: `tests/video/xer_video_smoke.cc` (strict blend expectations,
  ~lines 115-140)

**Interfaces:**
- Consumes: nothing new.
- Produces: corrected `VideoDecoder::Seek` fallback (stream time-base
  timestamp); corrected `XER::Bridge::Skia::BlendSrcOver` /
  `BlendModeApply("src_over"/"srcOver")` straight-alpha math — all
  consumers (`MixSystem::Mix`, `BlendModeApply` callers) fixed at once.

- [ ] **Step 1: Write the failing regression tests**

**1a. `tests/video/xer_video_smoke.cc`** — tighten the `MixSystem`
checks that currently accept EITHER backend's output. Replace the
"accept either ±4 LSB" blocks (~lines 121-140) with strict correct
values. After the fix BOTH backends must produce the straight-alpha
src-over result:

```cpp
        // Correct straight-alpha src-over (both backends must agree):
        // dst=(B10,G20,R30,A255), src=(B200,G100,R50,A255), w=0.5 →
        // a≈0.5 → out ≈ (105,60,40). The pre-fix Skia path blended in
        // premultiplied space and produced ≈(205,110,65) — regression
        // guard for the BlendSrcOver fix.
        const int mB = Channel(dst, 0, 0, 0);
        const int mG = Channel(dst, 0, 0, 1);
        const int mR = Channel(dst, 0, 0, 2);
        XER_VIDEO_CHECK(std::abs(mB - 105) <= 4);
        XER_VIDEO_CHECK(std::abs(mG - 60)  <= 4);
        XER_VIDEO_CHECK(std::abs(mR - 40)  <= 4);
```

and for the `Blend` case:

```cpp
            // a=black under b at t=0.25 → ≈(B25,G38,R50); both backends.
            const int bB = Channel(*blended, 0, 0, 0);
            const int bG = Channel(*blended, 0, 0, 1);
            const int bR = Channel(*blended, 0, 0, 2);
            XER_VIDEO_CHECK(std::abs(bB - 25) <= 4);
            XER_VIDEO_CHECK(std::abs(bG - 38) <= 4);
            XER_VIDEO_CHECK(std::abs(bR - 50) <= 4);
```

**1b. `tests/ui/ffmpeg_audio_tap_tests.cc`** — add a case pinning the
seek POSITION, not just drain-ability. Append a new `XER_UI_TEST`:

```cpp
// Regression for the Seek fallback bug: av_seek_frame with a stream
// index expects the STREAM time_base, but the fallback passed the
// AV_TIME_BASE timestamp unchanged — on audio-only files the seek
// landed at ~time_base_ratio x the target (e.g. 0.5 s requested could
// seek to ~10 s in a 1/48000 stream). After the fix, the first decoded
// audio frame's media time must be <= target + one AAC-ish frame and
// the drained total must match a from-target decode.
XER_UI_TEST(ffmpeg_decoder_audio_only_seek_position) {
    if (!XER::Bridge::FFmpeg::IsAvailable()) {
        return;
    }
    std::string err;
    const auto path = (TempDir() / "seek_pos.wav").string();
    // ~2 s stereo 48 kHz sine WAV (pcm_s16le — always available).
    XER::Bridge::FFmpeg::EncodeAudioParams params;
    params.output_path = path;
    params.codec_name = "pcm_s16le";
    params.sample_rate = kSampleRate;
    params.channels = kChannels;
    params.sample_format = "s16";
    const auto frames = MakeSineFrames(kSampleRate * 2);
    if (!XER::Bridge::FFmpeg::EncodeAudioFrames(params, frames, &err)) {
        return;  // fixture unavailable — skip.
    }

    XER::Bridge::FFmpeg::VideoDecoder decoder;
    decoder.EnableAudioTap();
    XER_UI_EXPECT_TRUE(decoder.Open(path, &err));
    if (!decoder.IsOpen()) return;

    XER_UI_EXPECT_TRUE(decoder.Seek(1.0));
    // First decoded frame's media time must be near 1.0 s — NOT near
    // the end of file (the old bug sought ~48000x too far and clamped
    // to EOF, draining ~0 samples).
    std::vector<std::uint8_t> bgra;
    int w = 0, h = 0;
    decoder.NextFrame(bgra, w, h);  // pump
    std::vector<XER::Bridge::FFmpeg::AudioFrameInfo> chunk;
    XER_UI_EXPECT_TRUE(decoder.ReadAudioFrames(&chunk, 4));
    bool saw_near_target = false;
    for (const auto& f : chunk) {
        const double tb = decoder.GetAudioTimeBase();
        const std::int64_t pts =
            f.best_effort_timestamp != std::numeric_limits<std::int64_t>::min()
                ? f.best_effort_timestamp : f.pts;
        if (tb > 0.0) {
            const double media_t = pts * tb;
            // Keyframe-less PCM seek lands within ~one packet of the
            // target — require <= target + 0.1 s and >= 0 (never past
            // EOF for a 2 s file).
            if (media_t >= 0.9 && media_t <= 1.1) saw_near_target = true;
        }
    }
    XER_UI_EXPECT_TRUE(saw_near_target);
    const long long total = DrainAudio(&decoder);
    // ~1 s of 48 kHz stereo remains after seeking to 1.0 s of 2 s.
    XER_UI_EXPECT_TRUE(total > 40000);
    XER_UI_EXPECT_TRUE(total < 70000);
    XER_UI_EXPECT_TRUE(decoder.AudioDrained());
}
```

(add `#include <limits>` if not already present in that file.)

- [ ] **Step 2: Build — verify RED**

Run: `"D:/Developers/XER3/out/vcenv.bat" cmake --build out/build/default --target XER_Video_Smoke XER_UI_Tests`
Then: `"D:/Developers/XER3/out/vcenv.bat" ctest --test-dir out/build/default --output-on-failure -R "XER_Video_Smoke|XER_UI_Tests"`
Expected: `XER_Video_Smoke` FAILS on the Skia-path blend values
(Windows always has Skia → the buggy ~205,110,65 result no longer
matches the strict ~105,60,40 assert); the new audio-seek test FAILS
or reports wrong position (the fallback `av_seek_frame` gets an
AV_TIME_BASE ts in a stream-indexed call → wrong stream position).
NOTE: `XER_UI_Tests --gtest_filter`-style selection is unreliable in
this harness — run the whole exe; the new test name appears in output.

- [ ] **Step 3: Fix `VideoDecoder::Seek` fallback**

`src/wrapper/ffmpeg/ffmpeg_video_decoder.cc` ~line 558-565. The primary
`avformat_seek_file(p.fmt, -1, ...)` uses stream_index -1 → AV_TIME_BASE
units (correct). The fallback uses a concrete stream index → timestamp
must be in THAT stream's time_base:

```cpp
    if (ret < 0) {
        const int fallback_stream =
            p.video_stream_index >= 0 ? p.video_stream_index
                                      : p.audio_stream_index;
        if (fallback_stream >= 0) {
            // av_seek_frame with a stream index expects the timestamp
            // in that stream's time_base — rescale from AV_TIME_BASE
            // (was: raw AV_TIME_BASE ts -> ~1e6/ tb_ratio too far).
            const AVStream* fs = p.fmt->streams[fallback_stream];
            const int64_t stream_ts = av_rescale_q(
                ts, AV_TIME_BASE_Q, fs->time_base);
            ret = av_seek_frame(p.fmt, fallback_stream, stream_ts,
                                AVSEEK_FLAG_BACKWARD);
        }
    }
```

- [ ] **Step 4: Fix `BlendSrcOver` straight-alpha math**

`src/wrapper/skia/skia_engine_bridge.cc` ~line 546. Current code
computes `out_c = src_c + dst_c*(1-sa)` — the PREMULTIPLIED-src-over
formula applied to STRAIGHT-alpha channels, plus `static_cast<uint8_t>`
wraps sums >255. Replace the body with the correct general
straight-alpha src-over (same math as `BlendSrcOver_ps` in
`skia_engine_bridge_fx.cc:207-218`, which is already correct — keep
integer-rounding style consistent with the file):

```cpp
uint32_t BlendSrcOver(uint32_t dst_rgba, uint32_t src_rgba) {
    // Straight-alpha src-over producing straight-alpha output:
    //   oa = sa + da*(1-sa)
    //   oc = (sc*sa + dc*da*(1-sa)) / oa        for c in {r,g,b}
    // The previous implementation used the premultiplied-src formula on
    // straight-alpha channels and let uint8 wrap past 255 — partial-alpha
    // blends (MixMode::Alpha, transitions) were visibly wrong.
    const float sa = GetA(src_rgba) / 255.0f;
    const float da = GetA(dst_rgba) / 255.0f;
    const float oa = sa + da * (1.0f - sa);
    if (oa <= 1e-6f) {
        return 0;
    }
    auto channel = [&](int shift) -> uint8_t {
        const float sv = ((src_rgba >> shift) & 0xFF) / 255.0f;
        const float dv = ((dst_rgba >> shift) & 0xFF) / 255.0f;
        const float v = (sv * sa + dv * da * (1.0f - sa)) / oa;
        return static_cast<uint8_t>(std::clamp(v * 255.0f + 0.5f,
                                               0.0f, 255.0f));
    };
    const uint8_t out_a = static_cast<uint8_t>(
        std::clamp(oa * 255.0f + 0.5f, 0.0f, 255.0f));
    // Channel shifts depend on PackRGBA's layout — verify GetR/GetG/
    // GetB/GetA shifts in this file and use the same shifts here.
    return PackRGBA(channel(24), channel(16), channel(8), out_a);
}
```

IMPLEMENTATION NOTE for the builder: `PackRGBA(r,g,b,a)` in this file
packs `0xRRGGBBAA` (R in bits 24-31) — confirm the `Get*`/`PackRGBA`
shift conventions at the top of `skia_engine_bridge.cc` and use the
same shifts in the lambda calls (the snippet assumes R=shift24,
G=16, B=8, matching `MakeColorRGBA`; if `GetR` uses a different shift,
match it). `std::clamp`/`std::min` are already used in this file —
include `<algorithm>`/`<cmath>` only if missing.

- [ ] **Step 5: Build + run — verify GREEN**

Same build + ctest commands. Expected: `XER_Video_Smoke` passes with
strict values; `ffmpeg_decoder_audio_only_seek_position` passes (first
frame media time ~1.0 s, drained total in (40000, 70000)).
Regression spot-check: `"D:/Developers/XER3/out/vcenv.bat" ctest
--test-dir out/build/default --output-on-failure -R "XER_UI|XER_Video"`
— `BlendModeApply` consumers elsewhere get the corrected math; any
other test that codified the buggy blend must be updated the same way
(grep `tests/` for `BlendSrcOver|src_over|MixMode::Alpha` first).

- [ ] **Step 6: Commit**

```bash
git add src/wrapper/ffmpeg/ffmpeg_video_decoder.cc \
        src/wrapper/skia/skia_engine_bridge.cc \
        tests/ui/ffmpeg_audio_tap_tests.cc \
        tests/video/xer_video_smoke.cc
git commit -m "Fix decoder seek timebase rescale and Skia src-over alpha math"
```

---

## Task 2: `media_decoder` abstraction (`IMediaDecoder` + bridge adapter)

The compositor and mixer need frame/audio decode, but their core logic
(track stacking, transitions, transforms, PTS selection, gain/fade
envelopes) must be unit-testable WITHOUT FFmpeg. `XER::Bridge::FFmpeg::
VideoDecoder` is a concrete class (no virtuals) — wrap it behind a small
interface; tests inject fakes. One instance == one open media file.

**Files:**
- Create: `tools/video_editor/media_decoder.h`,
  `tools/video_editor/media_decoder.cc`
- Create: `tests/video/video_editor_decoder_tests.cc` (new suite)
- Modify: `tests/video/video_editor_test_main.cc` (register suite)
- Modify: `tests/CMakeLists.txt` (add suite source)

**Interfaces:**
- Consumes: `wrapper/ffmpeg/ffmpeg_video_decoder.h`,
  `wrapper/ffmpeg/ffmpeg_engine_bridge.h` (adapter only).
- Produces (`tools/video_editor/media_decoder.h`, namespace
  `XER::VideoEditor`; standard includes `<cstdint> <functional>
  <memory> <string> <vector>` + `#pragma once`):

```cpp
namespace XER::Bridge::FFmpeg { struct AudioFrameInfo; }  // fwd — full
// def lives in ffmpeg_engine_bridge.h; callers that touch audio
// fields include that header themselves.

// Frame/audio decode abstraction over XER::Bridge::FFmpeg::VideoDecoder.
// Mirrors its contract exactly: NextFrame() decodes forward and
// reports the decoded frame's media time via GetPositionSeconds()
// (call it AFTER a successful NextFrame — Seek() presets it to the
// requested target until the next frame lands); with the audio tap
// enabled, NextFrame() also pumps demux packets into the tap queue —
// audio-only files return false but still pump (bounded batch/call);
// drive audio by alternating NextFrame()/ReadAudioFrames() until
// AudioDrained(). NOT internally synchronized — one consumer thread.
class IMediaDecoder {
public:
    virtual ~IMediaDecoder() = default;

    virtual bool Open(const std::string& utf8_path,
                      std::string* error_out = nullptr) = 0;
    virtual void Close() = 0;
    virtual bool IsOpen() const = 0;

    // ---- video ----
    virtual bool NextFrame(std::vector<std::uint8_t>* bgra,
                           int* width, int* height) = 0;
    virtual bool Seek(double seconds) = 0;
    virtual double GetPositionSeconds() const = 0;  // last frame's pts
    virtual double GetFps() const = 0;
    virtual double GetDurationSeconds() const = 0;
    virtual int GetWidth() const = 0;    // 0 = audio-only container
    virtual int GetHeight() const = 0;

    // ---- audio tap (EnableAudioTap BEFORE Open) ----
    virtual void EnableAudioTap(bool enable = true) = 0;
    virtual bool HasAudio() const = 0;
    virtual double GetAudioStartSeconds() const = 0;
    virtual double GetAudioTimeBase() const = 0;  // s per pts tick; 0 none
    virtual bool ReadAudioFrames(
        std::vector<XER::Bridge::FFmpeg::AudioFrameInfo>* out,
        int max_frames) = 0;
    virtual bool AudioDrained() const = 0;
    virtual std::uint64_t GetAudioEpoch() const = 0;
};

using MediaDecoderPtr = std::unique_ptr<IMediaDecoder>;
// Called with no args; the returned decoder is Open()ed by the caller.
// Production default wraps XER::Bridge::FFmpeg::VideoDecoder.
using MediaDecoderFactory = std::function<MediaDecoderPtr()>;

// Production factory — BridgeVideoDecoder (defined in media_decoder.cc).
MediaDecoderPtr MakeBridgeDecoder();
MediaDecoderFactory DefaultDecoderFactory();
```

- Consumes: nothing outside std + the wrapper headers (adapter .cc).
- Produces: `IMediaDecoder`, `BridgeVideoDecoder` (adapter),
  `MakeBridgeDecoder`/`DefaultDecoderFactory` — consumed by Tasks 4-6.

- [ ] **Step 1: Write the failing decoder tests**

Create `tests/video/video_editor_decoder_tests.cc` exposing
`int XER_VideoEditor_RunDecoderTests()`. Cases:

```cpp
// 1. Fake-decoder contract (no FFmpeg): a hand-rolled IMediaDecoder in
//    the test file feeds 3 synthetic BGRA frames at known pts — proves
//    the interface is implementable and documents expected semantics
//    (NextFrame fills bgra/w/h; GetPositionSeconds reports LAST frame's
//    pts; Seek resets position).
// 2. MakeBridgeDecoder() returns non-null; on FFmpeg-available builds
//    Open() on a synthesized 8-frame clip succeeds, NextFrame yields
//    64x64 BGRA, GetPositionSeconds advances ~1/24 s per frame, EOF
//    returns false. Self-skips via IsAvailable() and on encode failure.
// 3. Audio-tap passthrough: same fixture encoded with a sine WAV track
//    (or audio-only wav — reuse the tap-test pattern): EnableAudioTap
//    before Open, alternate NextFrame/ReadAudioFrames until
//    AudioDrained, verify AudioFrameInfo fields populate.
```

Register in `video_editor_test_main.cc`:

```cpp
int XER_VideoEditor_RunDecoderTests();
// ...
    XER_VideoEditor_RunDecoderTests();
```

and `tests/CMakeLists.txt` — append `video/video_editor_decoder_tests.cc`
to the `XER_VideoEditor_Tests` sources (after the poster line).

- [ ] **Step 2: Build — verify RED**

`"D:/Developers/XER3/out/vcenv.bat" cmake --build out/build/default --target XER_VideoEditor_Tests`
Expected: compile error — `media_decoder.h` not found.

- [ ] **Step 3: Implement `media_decoder.{h,cc}`**

Header: exact declarations above. `.cc`:

- `BridgeVideoDecoder` — `final` class holding
  `XER::Bridge::FFmpeg::VideoDecoder decoder_;` (by value, RAII —
  `~VideoDecoder` calls `Close()`). Every method is a one-line
  pass-through; `NextFrame` adapts the bridge's ref-out signature to
  pointers (`if (bgra) decoder_.NextFrame(*bgra, w_ref, h_ref)` —
  bridge takes `std::vector<uint8_t>&, int&, int&`; use local refs then
  assign through pointers, nullptr-tolerant on the pointer args per
  house style but REQUIRE bgra/width/height non-null for NextFrame —
  document "undefined if null" or guard: simplest is
  `if (bgra == nullptr || width == nullptr || height == nullptr)
  return false;`).
- `EnableAudioTap` forwards before Open; document the ordering
  constraint in the header comment (already in the interface doc).
- `MakeBridgeDecoder()` → `std::make_unique<BridgeVideoDecoder>()`.
- `DefaultDecoderFactory()` → returns `&MakeBridgeDecoder`-compatible
  `MediaDecoderFactory{[] { return MakeBridgeDecoder(); }}`.
- Off-Windows: `VideoDecoder` methods are stubs returning false/0 —
  the adapter compiles unchanged (same pattern as `clip_poster.cc` —
  no `#ifdef` needed).

- [ ] **Step 4: Build + run — verify GREEN**

Build `XER_VideoEditor_Tests`, run the exe (or `ctest -R
XER_VideoEditor`). Expected: decoder suite passes; FFmpeg cases skip
cleanly when unavailable.

- [ ] **Step 5: Commit**

```bash
git add tools/video_editor/media_decoder.{h,cc} \
        tests/video/video_editor_decoder_tests.cc \
        tests/video/video_editor_test_main.cc tests/CMakeLists.txt
git commit -m "Add IMediaDecoder abstraction for VideoEditor decode paths"
```

---

## Task 3: `EncodeAudioStream` — audio-only streaming encode bridge API

`EncodeAudioFrames` is batch-only (full `std::vector<AudioFrameInfo>`
in RAM — a 30-min stereo mix is ~700 MB of PCM chunks). The mux API
`EncodeAudioVideoStream` requires ≥1 video frame, so it cannot serve
audio-only projects. Spec text says "audio-only via EncodeAudioFrames"
but the anti-goal says audio streams PCM chunks — add the symmetric
streaming API; it reuses the anonymous-namespace encoder helpers
(`SetupAudioEncoder`/`FeedAudioEncoderFrame`/`FlushAudioEncoderFifo`/
`FlushEncoderCodec`/`OpenOutputAndWriteHeader`) designed for exactly
this composition (`ffmpeg_engine_bridge.cc:140` note in facts doc).

**Files:**
- Modify: `src/wrapper/ffmpeg/ffmpeg_engine_bridge.h` (decl ~line 234)
- Modify: `src/wrapper/ffmpeg/ffmpeg_engine_bridge.cc` (impl after
  `EncodeAudioFrames`, ~line 2255)
- Modify: `tests/ui/ffmpeg_av_mux_tests.cc` (new cases)

**Interfaces:**
- Produces (`ffmpeg_engine_bridge.h`, namespace
  `XER::Bridge::FFmpeg`, next to `EncodeAudioFrames`):

```cpp
// Streams provider-produced PCM chunks into a single audio-only
// container (params.output_path authoritative). frame_count chunks are
// requested with frame_index in [0, frame_count) exactly once; chunk 0
// is fetched during encoder setup to fix the input format, and EVERY
// chunk must share sample_rate/channels/sample_format/planar (the
// encoder rejects inconsistent streams, same contract as
// EncodeAudioFrames). frame_count == 0 is an error; a false provider
// return aborts with error_out set. On failure a partial file may
// remain at output_path — callers doing atomic output must encode to a
// temp path (ExportEngine does).
bool EncodeAudioStream(const EncodeAudioParams& params,
                       std::size_t frame_count,
                       const AudioFrameStreamProvider& provider,
                       std::string* error_out);
```

(`AudioFrameStreamProvider` already exists at `ffmpeg_engine_bridge.h:249`.)

- Consumes: existing anonymous-namespace helpers — no signature changes.
- Produces: consumed by `ExportEngine` Task 6 for the no-video path.

- [ ] **Step 1: Write the failing tests**

Append to `tests/ui/ffmpeg_av_mux_tests.cc` (XER_UI_TEST harness,
self-skip via `IsAvailable()`):

```cpp
// ~1.5 s stereo 48 kHz sine in <=1024-sample chunks through the new
// streaming API -> ProbeMedia shows exactly one AUDIO stream; decode a
// few frames via DecodeAudioFrames and check sample_rate/channels.
XER_UI_TEST(ffmpeg_encode_audio_stream_muxes_audio_only)

// frame_count == 0 -> false + non-empty error (mirrors the
// video_frame_count==0 contract of EncodeAudioVideoStream).
XER_UI_TEST(ffmpeg_encode_audio_stream_rejects_empty)

// Provider dies at chunk 3 -> false; file may exist partially (do NOT
// assert absence — atomic cleanup is the caller's job).
XER_UI_TEST(ffmpeg_encode_audio_stream_provider_error)

// Consistency: chunk 5 reports a different sample_rate -> false
// ("requires a consistent audio stream" via FeedAudioEncoderFrame).
XER_UI_TEST(ffmpeg_encode_audio_stream_rejects_inconsistent_format)
```

Fixture helpers already exist in that file (`FillAudioChunk`,
`AudioProvider` pattern) — reuse them; the new provider wraps
`AudioProvider` with an index guard.

- [ ] **Step 2: Build — verify RED**

`"D:/Developers/XER3/out/vcenv.bat" cmake --build out/build/default --target XER_UI_Tests` → `EncodeAudioStream` not declared.

- [ ] **Step 3: Implement**

Header: add the declaration + doc comment exactly as above, placed
after `EncodeAudioFrames` (before `EncodeAudioVideoParams`).

`.cc`: add after `EncodeAudioFrames` (~line 2254), same `#if
ENGINE_HAS_FFMPEG_BRIDGE` / `#else` stub shape:

```cpp
bool EncodeAudioStream(const EncodeAudioParams& params,
                       std::size_t frame_count,
                       const AudioFrameStreamProvider& provider,
                       std::string* out_error) {
#if ENGINE_HAS_FFMPEG_BRIDGE
    if (frame_count == 0u) {
        SetError(out_error, "EncodeAudioStream requires at least one audio frame");
        return false;
    }
    if (!provider) {
        SetError(out_error, "EncodeAudioStream requires an audio frame provider");
        return false;
    }
    if (params.output_path.empty()) {
        SetError(out_error, "EncodeAudioStream requires an output path");
        return false;
    }

    AudioFrameInfo first_frame;
    if (!provider(0u, &first_frame, out_error)) {
        return false;
    }
    if (first_frame.sample_rate <= 0 || first_frame.channels <= 0 ||
        first_frame.sample_count <= 0) {
        SetError(out_error, "EncodeAudioStream received invalid first audio frame");
        return false;
    }

    AVFormatContext* output_context = nullptr;
    int result = avformat_alloc_output_context2(&output_context, nullptr,
                                              nullptr,
                                              params.output_path.c_str());
    if (result < 0 || output_context == nullptr) {
        SetError(out_error, "avformat_alloc_output_context2 failed: " +
                            ErrorString(result));
        return false;
    }

    AudioEncodeState encode_state;
    AVPacket* packet = nullptr;
    bool success = SetupAudioEncoder(output_context, params, first_frame,
                                     &encode_state, out_error);
    if (success) {
        success = OpenOutputAndWriteHeader(output_context,
                                           params.output_path, out_error);
    }
    const bool header_written = success;
    if (success) {
        packet = av_packet_alloc();
        if (packet == nullptr) {
            success = false;
            SetError(out_error, "av_packet_alloc failed");
        }
    }
    if (success) {
        for (std::size_t i = 0; i < frame_count; ++i) {
            AudioFrameInfo input;
            if (i == 0u) {
                input = std::move(first_frame);
            } else if (!provider(i, &input, out_error)) {
                success = false;
                break;
            }
            if (!FeedAudioEncoderFrame(output_context, &encode_state, input,
                                       packet, out_error)) {
                success = false;
                break;
            }
        }
        if (success &&
            !FlushAudioEncoderFifo(output_context, &encode_state, packet,
                                   out_error)) {
            success = false;
        }
        if (success &&
            !FlushEncoderCodec(output_context, encode_state.codec_context,
                               encode_state.stream, packet, out_error)) {
            success = false;
        }
    }

    if (header_written) {
        const int trailer_result = av_write_trailer(output_context);
        if (success && trailer_result < 0) {
            success = false;
            SetError(out_error, "av_write_trailer failed: " +
                                ErrorString(trailer_result));
        }
    }

    if (packet != nullptr) {
        av_packet_free(&packet);
    }
    if ((output_context->oformat->flags & AVFMT_NOFILE) == 0 &&
        output_context->pb != nullptr) {
        avio_closep(&output_context->pb);
    }
    ReleaseAudioEncodeState(&encode_state);
    avformat_free_context(output_context);
    return success;
#else
    (void)params;
    (void)frame_count;
    (void)provider;
    SetError(out_error, "FFmpeg bridge unavailable");
    return false;
#endif
}
```

NOTE: mirror the exact `#if`/`#else` tail of `EncodeAudioFrames`
(lines 2250-2253) — copy its structure verbatim.

- [ ] **Step 4: Build + run — verify GREEN**

`"D:/Developers/XER3/out/vcenv.bat" cmake --build out/build/default --target XER_UI_Tests` then run `out/build/default/bin/XER_UI_Tests.exe` (vcenv for direct exe paths is mangled — use `ctest -R XER_UI_Tests` or absolute path under vcenv).

- [ ] **Step 5: Commit**

```bash
git add src/wrapper/ffmpeg/ffmpeg_engine_bridge.{h,cc} \
        tests/ui/ffmpeg_av_mux_tests.cc
git commit -m "Add EncodeAudioStream for audio-only streaming export"
```

---

## Task 4: `timeline_model` transition handles + `compositor`

The biggest task. `Compositor::RenderFrameAt(t)` renders canvas BGRA at
timeline time `t`: per video track, find contributing clips (nominal
`[start,end)` PLUS transition handle extensions `duration/2` past each
endpoint), decode each lazily/sequentially with nearest-PTS selection,
apply the CPU effect chain, warp by the affine transform into a
canvas-sized layer, combine transition pairs, alpha-over tracks
bottom→top onto `bg_color`.

**Files:**
- Modify: `tools/video_editor/timeline_model.h`,
  `tools/video_editor/timeline_model.cc` (add `ClipTransitionHandles`)
- Modify: `tests/video/video_editor_model_tests.cc` (handle cases)
- Create: `tools/video_editor/compositor.h`,
  `tools/video_editor/compositor.cc`
- Create: `tests/video/video_editor_compositor_tests.cc` (new suite)
- Modify: `tests/video/video_editor_test_main.cc`,
  `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `timeline_model` (Task 1B), `media_decoder` (Task 2),
  `video::Frame`, `video::VideoEffects`, `wrapper/skia/
  skia_engine_bridge.h` (`IsAvailable` for effect warnings only).
- Produces — `timeline_model.h` addition (namespace
  `XER::VideoEditor`):

```cpp
// Handle seconds a clip's contribution window extends past its
// nominal [timeline_start, end) range: {in_handle, out_handle} where
// in_handle = duration/2 of the transition where clip is `to`,
// out_handle = duration/2 where clip is `from` (0 when none). Both
// compositor (video) and audio_mixer (audio) consume this — the
// transition window is centered on the cut and reaches into source
// handles per the spec's handle model.
std::pair<double, double> ClipTransitionHandles(
    const Project& project, const std::string& clip_id);
```

- Produces — `tools/video_editor/compositor.h` (namespace
  `XER::VideoEditor`):

```cpp
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "frame.h"            // video::Frame — XER_Video (PUBLIC dep)
#include "media_decoder.h"    // IMediaDecoder + MediaDecoderFactory
#include "timeline_model.h"

namespace XER::VideoEditor {

// ---- pure helpers (exposed for tests + 1D preview reuse) ----

// Normalized transition progress w in [0,1]: 0 at window start
// (cut - duration/2), 1 at window end (cut + duration/2), clamped.
// `cut_seconds` = ClipEndSeconds(from_clip) == to_clip.timeline_start.
double TransitionWeightAt(double t_seconds, const Transition& transition,
                          double cut_seconds);

// Composites two canvas-sized straight-alpha BGRA layers — `under`
// (from/outgoing) and `over` (to/incoming) — per the transition at
// weight w, into `out` (canvas-sized, may not alias inputs). All three
// frames must share width/height/format(BGRA8).
//   Crossfade: premultiplied-alpha lerp (correct for straight-alpha
//     layers; equals MixSystem::Blend(Alpha,w) for opaque inputs).
//   Wipe<D>: hard-edge reveal — `over` shows where the directional
//     test passes (see geometry below), `under` elsewhere; inside the
//     revealed region `over` alpha-composites OVER `under`.
//   Slide<D>: BOTH layers translate toward D — under exits toward D
//     by w*dim while over enters from the opposite side by
//     (1-w)*dim (convention fixed in the .cc doc below).
// Returns false on size/format mismatch.
bool ApplyTransitionPixels(Transition::Type type, double weight,
                           const video::Frame& under,
                           const video::Frame& over,
                           video::Frame* out);

// Renders `src` (packed BGRA8, any dims) into a canvas-sized
// transparent-black layer applying `tr`: the scaled rect
// (src_w*scale × src_h*scale) is placed with its TOP-LEFT at
// (offset_x*canvas_w, offset_y*canvas_h) — the 1B convention — then
// rotated `rotation_deg` about the scaled rect's CENTER, and alpha is
// multiplied by `opacity`. Inverse-mapped bilinear sampling; pixels
// outside the source stay transparent (alpha 0). `out_layer` must be
// canvas_w*canvas_h BGRA8 (allocated by caller or here).
bool WarpClipFrame(const video::Frame& src, const Transform& tr,
                   int canvas_w, int canvas_h, video::Frame* out_layer);

// ---- the compositor ----

struct CompositorOptions {
    // Empty → DefaultDecoderFactory() (real FFmpeg bridge). Tests
    // inject fakes.
    MediaDecoderFactory decoder_factory;
};

// Frame-accurate timeline → canvas BGRA renderer. UI-free, single
// consumer thread (RenderFrameAt calls must be serialized — the
// export worker and a future preview engine each own an instance).
// Init() snapshots the project (copies it); decoders open lazily on a
// clip's first contributing frame and close after its last.
class Compositor {
public:
    Compositor();
    ~Compositor();
    Compositor(const Compositor&) = delete;
    Compositor& operator=(const Compositor&) = delete;

    // Binds a snapshot of `project`. Fails only on invalid canvas
    // (width/height <= 0) or empty fps — clip-level problems (missing
    // files, undecodable media, audio-only-on-video-track) surface as
    // RenderFrameAt errors or Warnings(), not Init failures.
    // `warnings_out` (optional) collects non-fatal notes: unknown
    // ClipEffect.type strings, Skia-only effects when Skia is
    // unavailable, transitions referencing clips not on video tracks.
    bool Init(const Project& project, const CompositorOptions& options,
              std::string* error_out = nullptr,
              std::vector<std::string>* warnings_out = nullptr);

    void Shutdown();   // close all decoders + release frames (idempotent)

    int CanvasWidth() const;
    int CanvasHeight() const;
    double DurationSeconds() const;    // project.duration()
    // Output frame count for fps_num/fps_den:
    // llround(duration * fps_num / fps_den), >= 1 when duration > 0.
    std::size_t FrameCount(int fps_num, int fps_den) const;

    // Renders canvas at timeline time `t` (seconds; clamped to
    // [0, duration]) into `out_bgra`: width*height*4 packed BGRA8,
    // row-major, stride width*4. Returns false + error_out on decode/
    // render failure (missing file, undecodable clip, allocation) —
    // export treats that as fatal. After Cancel(), the next call
    // returns false with "export cancelled"-style error.
    bool RenderFrameAt(double t, std::vector<std::uint8_t>* out_bgra,
                       std::string* error_out = nullptr);

    // Non-fatal issues seen so far (Init warnings + runtime notes like
    // "clip X has no video stream"). Cleared by Init/Shutdown.
    const std::vector<std::string>& Warnings() const { return warnings_; }

    void Cancel();          // cooperative; checked per RenderFrameAt
    bool Cancelled() const;

private:
    // defined in compositor.cc
};

}  // namespace XER::VideoEditor
```

- Consumes/Produces as listed. The lib glob picks up both .cc files
automatically — no CMake edit needed for the lib itself.
  LINK NOTE: `XER_BridgeSkia` (`IsAvailable`) and `XER_Cache`
  arrive TRANSITIVELY via `XER_Video`'s PUBLIC links
  (`src/video/CMakeLists.txt:23-47`) — `XER_VideoEditor_Core` already
  links `XER_Video` PUBLIC, so NO `target_link_libraries` edit is
  needed in Tasks 4-6. Only Task 7 touches this CMakeLists.

- [ ] **Step 1: Add `ClipTransitionHandles` to the model + tests**

`timeline_model.h` — add the declaration above near the transition
queries. `timeline_model.cc`:

```cpp
std::pair<double, double> ClipTransitionHandles(
    const Project& project, const std::string& clip_id) {
    double in_handle = 0.0, out_handle = 0.0;
    for (const auto& t : project.transitions) {
        if (t.to_clip == clip_id) {
            in_handle = std::max(in_handle, t.duration * 0.5);
        }
        if (t.from_clip == clip_id) {
            out_handle = std::max(out_handle, t.duration * 0.5);
        }
    }
    return {in_handle, out_handle};
}
```

(`max` guards the degenerate case of two transitions sharing an
endpoint — model invariants prevent it, but loaded/self-healed
projects could theoretically contain it; last-writer vs max is a
builder call — `max` is the safer monotone choice. Document.)

Model tests (`video_editor_model_tests.cc`, extend the transitions
block): clip `a`→`b` crossfade D=1.0 ⇒ handles(a)={0,0.5},
handles(b)={0.5,0}; non-transitioned clip → {0,0}; clip in TWO
transitions (a→b, b→c) → b gets {0.5,0.5}.

- [ ] **Step 2: Write the failing compositor tests**

Create `tests/video/video_editor_compositor_tests.cc` exposing
`int XER_VideoEditor_RunCompositorTests()`. Structure: a `FakeDecoder`
class implementing `IMediaDecoder` (defined in the test file) that
serves scripted BGRA frames at scripted pts — this is THE key
testability win; plus pure-helper tests needing no decoder at all.

Required cases:

```cpp
// -- pure helpers (no decoder) ------------------------------------
// TransitionWeightAt: w=0 at cut-D/2, 0.5 at cut, 1 at cut+D/2,
// clamped outside; D<=0 → w=1 for t>=cut (degenerate).
// WarpClipFrame: identity transform on a solid 4x4 src into 8x8
//   canvas → top-left quadrant filled, rest transparent; scale=2
//   fills whole canvas; offset moves the rect; opacity=0.5 → alpha
//   halved, RGB unchanged (straight alpha); rotation=90 maps corners.
// ApplyTransitionPixels: crossfade w=0 → under verbatim, w=1 → over
//   verbatim, w=0.5 → midpoint on opaque pixels; wipe-left w=0.5 →
//   left half = over, right half = under (hard edge at x=W/2);
//   slide-left w=0.5 → left half shows under's right portion... (pin
//   the exact convention implemented in Step 4 — write expected values
//   from the documented math, not from observed output).

// -- compositor with FakeDecoder ----------------------------------
// Canvas/background: 16x16 project, one clip 0-1s, fake decoder with
//   4 frames @24fps solid red → RenderFrameAt(0.0) returns 16x16 BGRA,
//   every pixel red (B=0? — use distinct channel values, e.g. src
//   B=10,G=20,R=200), alpha 255.
// Track stacking: two video tracks, bottom blue clip + top 50%-alpha
//   red clip offset to cover only left half → left half blends,
//   right half stays blue; hidden top track → pure blue.
// Active-clip boundary: clip A 0-1s, clip B 1-2s same track →
//   RenderFrameAt(0.999) all A-color, (1.0) all B-color ([) semantics).
// Transform: offset/scale/rotation/opacity on a fake-decoded frame —
//   verify via WarpClipFrame-level asserts AND one RenderFrameAt smoke.
// Effects: clip.effects = [{"brightness_contrast",{10,1.0}}] → decoded
//   pixel +10 per channel; unknown type "bogus" → skipped + warning in
//   Warnings().
// Transition crossfade: A 0-2s, B 2-4s, transition D=1 → t=1.75 (w≈0)
//   ≈A, t=2.0 ≈50/50 mix of A/B solid colors, t=2.25 ≈B.
// Transition wipe-left D=1 → t=1.75 left quarter over? (compute per
//   convention), t=2.0 left half over.
// Transition slide-left → verify directional offset at w=0.5.
// Handle/EOF freeze: fake decoder with only 2 frames for a 4s clip →
//   frames past its last pts repeat the last frame (no black hole).
// Sequential-decode/PTS filter: fake decoder emits frames at pts
//   0, 1/24, 2/24… — RenderFrameAt picks the NEAREST frame to
//   in_point+(t-start), including the "previous" candidate (a target
//   just past a frame boundary must not always round up).
// Sparse/missing pts: fake decoder reports GetPositionSeconds=-1 or
//   jumps — compositor falls back to decode-order (document expected
//   behavior in test comment; assert no crash + deterministic output).
// Audio-only-on-video-track: fake decoder GetWidth()==0 → clip
//   contributes nothing; canvas stays bg_color; warning recorded.
// Muted clip: clip.muted does NOT hide video (still composites).
// Hidden track: contributes neither video nor (implicitly) audio.
// Empty regions: t before first clip / after last → bg_color pixels.
// Cancel(): Init, Cancel(), RenderFrameAt → false + cancelled error.
// Two clips same file far apart: fake factory counts Open() calls →
//   exactly 2 decoder instances (per-clip-instance lifecycle), and a
//   decoder opened for clip B does NOT see clip A's frames.
// Duration/FrameCount: project.duration()=4s @ fps 30/1 → FrameCount
//   =120; @ 30000/1001 → llround(4*30000/1001)=120.
```

FFmpeg-gated integration case (self-skip): synthesize a real clip via
`EncodeVideoFrameStream` (pattern from `ffmpeg_audio_tap_tests.cc`
`EncodeTinyVideo`) → Compositor with `DefaultDecoderFactory` renders
frame 0 → non-bg pixels present. Keep it minimal — the fake-decoder
cases carry the logic coverage.

- [ ] **Step 3: Build — verify RED**

Build `XER_VideoEditor_Tests` → `compositor.h`/`ClipTransitionHandles` not found.

- [ ] **Step 4: Implement `ClipTransitionHandles` + `compositor.{h,cc}`**

**4a. Model helper** — as in Step 1.

**4b. `compositor.cc` structure** (anonymous-namespace helpers + class):

```cpp
namespace {

// ---- pixel helpers (packed BGRA8, straight alpha) ----
// AlphaOver(dst, src): per-pixel src-over, straight→straight:
//   sa=src.a/255, da=dst.a/255, oa=sa+da*(1-sa)
//   out_c = (src_c*sa + dst_c*da*(1-sa))/oa   (guard oa≈0 → 0)
//   out_a = oa*255
// CrossfadePixels(under, over, w): premultiplied lerp —
//   pa = ua*(1-w)+oa*w; pr = (ur*ua*(1-w)+orr*oa*w)/pa …
// WipePixel(type, x, y, W, H, w): directional test → bool revealed.
// SlideOffsets(type, w, W, H) → (under_dx,under_dy,over_dx,over_dy).
//
// ---- decode session ----
struct ClipSession {
    const Clip* clip;
    const Track* track;
    MediaDecoderPtr decoder;
    bool open_attempted=false, open_ok=false, no_video=false, eof=false;
    video::Frame prev, cur;         // last two decoded frames
    double pts_prev=-1, pts_cur=-1;
    bool has_prev=false, has_cur=false;
    video::Frame scratch;           // source-size copy for effects
    video::Frame layer;             // canvas-size warp output
    double media_duration=0, fps=30;
    int src_w=0, src_h=0;
    double ext_start=0, ext_end=0;  // contribution window
    bool WarnedNoVideo=false;
};
```

`Init`: copy `project` into member `project_`; validate
`width>0 && height>0 && fps_num>0 && fps_den>0`; scan clips' effects
for the curated type table — unknown types → warning; Skia-only types
(see table below) when `!XER::Bridge::Skia::IsAvailable()` → warning;
build `sessions_` map clip_id→ClipSession (precompute `ext_start/
ext_end` via `ClipTransitionHandles` + nominal range); store
`video_tracks_` = indices of Video-kind tracks.

`RenderFrameAt(t)`:
1. `t = clamp(t, 0, duration_)`; fill member `canvas_` Frame
   (Allocate once, reuse) with `bg_color` — write BGRA bytes:
   `p[0]=bg[2], p[1]=bg[1], p[2]=bg[0], p[3]=bg[3]` ({r,g,b,a} order).
2. For each video track index in `tracks_` order (bottom→top), skip
   `track.hidden`:
   a. Candidates = sessions on this track with `ext_start <= t <
      ext_end`.
   b. 0 → continue. 1 → render it alone (step d). ≥2 → resolve:
      i. Find a stored transition T (precomputed map keyed by
         (from_clip,to_clip)) whose window
         `[cut - D/2, cut + D/2)` contains t AND both endpoint clips
         are in candidates → render both layers via RenderClipLayer
         and combine with
         `ApplyTransitionPixels(T.type, w=TransitionWeightAt(t,T,cut),
         under=from_layer, over=to_layer)`.
         NOTE: during `[cut-D/2, cut)` the FROM clip is still
         nominally active while the TO clip sits in its handle region
         — do NOT "prefer nominal" here; the transition pair wins.
      ii. No matching transition (degenerate overlap, e.g. a very
         short middle clip inside two long transition windows) →
         pick the candidate whose nominal `[timeline_start, end)`
         contains t; if none, the one with max `timeline_start <= t`;
         warn once per project ("ambiguous transition overlap").
   d. AlphaOver `track_layer` onto `canvas_` (skip when layer fully
      transparent — cheap bbox check optional).
3. Copy `canvas_` → `*out_bgra` (resize + memcpy).

`RenderClipLayer(session, t)`:
1. If `clip->transform.opacity <= 0 || clip->transform.scale <= 0` →
   return transparent layer (skip decode entirely — optimization).
2. `src_t = clip->in_point + (t - clip->timeline_start)`; clamp to
   `[0, media_duration]` when `media_duration > 0` (else `[0, ∞)` —
   decoder EOF handles the end).
3. `EnsureFrame(session, src_t)` → `const video::Frame* src`:
   - Lazy open: factory → `Open(source_path)`; `EnableAudioTap` NOT
     called (video path doesn't need it — mixer owns audio decoders).
   - `GetWidth()<=0` → `no_video=true`, warn once, return nullptr
     (clip contributes nothing).
   - First call: `Seek(max(0, src_t - 0.25))` best-effort (ignore
     failure — forward decode still works).
   - Selection loop (see "decode strategy" below) → prev/cur pick.
   - EOF with no frames ever → error_out "no decodable frames".
4. Effects: if `clip->effects` non-empty → copy `*src` into
   `session.scratch` (Allocate only when dims change; else memcpy
   into existing Data()), run `ApplyClipEffectChain`, warp from
   scratch; else warp directly from `*src`.
5. `WarpClipFrame(src_or_scratch, clip->transform, W, H,
   &session.layer)` → return layer.

`EnsureFrame(session, target)` — the sequential decode/PTS filter
(implement EXACTLY this — the REFINEMENT below is normative):

```
loop (bounded by a decode-step sanity cap, e.g. 1e6):
    if !has_cur && !eof → NextFrame + shift prev/cur + pts_cur
    if eof && !has_cur            → error "no decodable frames"
    if has_cur && pts_cur >= target → pick nearer of {prev,cur},
                                      return (decode stops here)
    if eof                        → return cur (last frame = freeze)
    else                          → decode next, continue
```

REFINEMENT — exact selection rule (implement precisely):
- After each successful `NextFrame`, `pts_cur =
  decoder->GetPositionSeconds()`; if `pts_cur < 0` (no timestamps)
  synthesize `pts_cur = last_pts + 1/fps` so ordering still works.
- Serve(target): while `!(has_cur && pts_cur >= target)` and not eof
  → decode next (shift prev/cur). Then choose nearer of
  {prev (has_prev), cur} by `|pts - target|`; ties → prev (earlier).
  If eof reached with no `pts_cur >= target` → return cur (last frame)
  = freeze. If NO frame ever decoded → error.
- Decode-order note: frames arrive in decode order; for monotonic pts
  this yields true nearest-frame selection with ≤1 extra decode per
  call beyond the target — and ZERO seeks after the first.

Session close: after rendering track layers, for each session with
`t >= session.ext_end` → `decoder->Close()` + `Release()` frames
(shutdown half of lifecycle). Sessions never opened for clips whose
window hasn't started — lazy by construction.

**Effect dispatch table** (anonymous namespace; params clamped,
missing params → documented defaults):

| `ClipEffect.type`        | `video::VideoEffects` call                     | params            | Skia-only? |
|--------------------------|-----------------------------------------------|-------------------|------------|
| `brightness_contrast`    | `BrightnessContrast(f, int p0, float p1)`     | brightness, contrast (defaults 0, 1.0) | no (CPU) |
| `grayscale`              | `Grayscale(f)`                                | —                 | no (CPU fallback) |
| `box_blur`               | `BoxBlur(f, int p0)`                          | radius (def 4, clamp ≥1) | no (CPU fallback) |
| `gaussian_blur`          | `GaussianBlur(f, double p0)`                  | sigma (def 2.0)   | no (BoxBlur fallback) |
| `flip_h`                 | `FlipH(f)`                                    | —                 | no |
| `flip_v`                 | `FlipV(f)`                                    | —                 | no |
| `sharpen`                | `Sharpen(f, double p0)`                       | amount (def 1.5)  | YES |
| `edge_detect`            | `EdgeDetect(f)`                               | —                 | YES |
| `emboss`                 | `Emboss(f)`                                   | —                 | YES |
| `vignette`               | `Vignette(f, double p0, double p1)`           | strength, radius (def 0.5, 0.7) | YES |
| `chromatic_aberration`   | `ChromaticAberration(f, double p0)`           | offset (def 2.0)  | YES |
| `pixelate`               | `Pixelate(f, int p0)`                         | block_size (def 8) | YES |
| `oil_paint`              | `OilPaint(f, int p0)`                         | radius (def 4)    | YES |
| `motion_blur`            | `MotionBlur(f, double p0, int p1)`            | angle, distance (def 0, 10) | YES |

Unknown `type` → skip + warning `"unknown clip effect type '<t>'"`.
Skia-only types when `!IsAvailable()` → skip at apply time + warning
`"effect '<t>' requires Skia; skipped"` (Init pre-scans and warns
early too). ALL VideoEffects calls are no-ops on invalid frames —
guard `frame.IsValid()` upstream.

**`WarpClipFrame` math** (implement exactly — this is the affine
contract 1D preview must match):

```
dw = src_w * scale;  dh = src_h * scale          // uniform scale
ox = offset_x * canvas_w; oy = offset_y * canvas_h   // top-left of
                                                      // SCALED rect
cx = ox + dw/2; cy = oy + dh/2                  // rotation center
// Inverse map per dest pixel (x,y) [pixel centers +0.5]:
//   vx = x + 0.5 - cx;  vy = y + 0.5 - cy
//   if rotation != 0: rotate (vx,vy) by -θ
//   if |ux| > dw/2 or |uy| > dh/2 → outside → alpha 0
//   sx = ux/scale + src_w/2;  sy = uy/scale + src_h/2
//   bilinear sample src at (sx,sy) — clamp taps to [0,src_w-1]x[0,src_h-1]
//   out_a = src_a * opacity; out_rgb = src_rgb (straight alpha)
```

Iterate only the integer bbox of the rotated rect (transform the 4
corners, floor/ceil, clamp to canvas) — offscreen clips cost ~nothing.
Bilinear always (no nearest fast path in v1 — document).

**`ApplyTransitionPixels` conventions** (FIXED — document in header):

- `weight` already clamped [0,1] by caller via `TransitionWeightAt`.
- **Crossfade**: per-pixel premultiplied lerp (formula above).
- **WipeLeft**: `over` revealed where `(x+0.5)/W < w` — incoming grows
  from the LEFT edge rightward. **WipeRight**: `(x+0.5)/W > 1-w`.
  **WipeUp**: `(y+0.5)/H < w`. **WipeDown**: `(y+0.5)/H > 1-w`.
  Inside the revealed region the result is `AlphaOver(under, over)`
  (over's transparency lets under show — correct for transformed
  clips); outside → `under` verbatim. Hard edge, no feather (v1).
- **SlideLeft**: BOTH layers translate LEFTward — `under` displaced
  `-w*W` px (exits left), `over` displaced `+(1-w)*W` px (enters from
  right). SlideRight mirrors (+x). SlideUp/SlideDown on y. Per canvas
  pixel: `under_px = sample(under, x - udx, y - udy)`,
  `over_px = sample(over, x - odx, y - ody)` (outside → transparent),
  result = `AlphaOver(under_px, over_px)`. At w=0 → under only; w=1 →
  over only. Naming convention: `Slide<D>` = composite motion toward
  `<D>` (documented builder call).

- [ ] **Step 5: Build + run — verify GREEN**

`"D:/Developers/XER3/out/vcenv.bat" cmake --build out/build/default --target XER_VideoEditor_Tests` then run the exe. Expected: model + compositor suites pass; FFmpeg-gated case skips or passes.

- [ ] **Step 6: Commit**

```bash
git add tools/video_editor/timeline_model.{h,cc} \
        tools/video_editor/compositor.{h,cc} \
        tests/video/video_editor_model_tests.cc \
        tests/video/video_editor_compositor_tests.cc \
        tests/video/video_editor_test_main.cc tests/CMakeLists.txt
git commit -m "Add VideoEditor compositor: decode, effects, affine, transitions"
```

---

## Task 5: `audio_mixer`

Mixes all audio-bearing clips to 48 kHz stereo s16 chunks for the mux
provider. Per clip: own `IMediaDecoder` with `EnableAudioTap(true)`
BEFORE `Open` — separate from the compositor's video decoder (two
demux sessions per A/V file; spec-mandated pump cost, documented).

**Files:**
- Create: `tools/video_editor/audio_mixer.h`,
  `tools/video_editor/audio_mixer.cc`
- Create: `tests/video/video_editor_audio_mixer_tests.cc`
- Modify: `tests/video/video_editor_test_main.cc`,
  `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `timeline_model`, `media_decoder`,
  `wrapper/ffmpeg/ffmpeg_engine_bridge.h` (`AudioFrameInfo` — full
  def needed here).
- Produces (`tools/video_editor/audio_mixer.h`, namespace
  `XER::VideoEditor`) — includes `"media_decoder.h"` (for
  `MediaDecoderFactory`) and forward-declares
  `namespace XER::Bridge::FFmpeg { struct AudioFrameInfo; }` (the
  `GetChunk` signature only needs the pointer; `.cc` and callers that
  fill the struct include `ffmpeg_engine_bridge.h`):

```cpp
struct AudioMixerOptions {
    MediaDecoderFactory decoder_factory;   // empty → default bridge
    int sample_rate = 48000;               // output rate (fixed)
    int channels = 2;                      // output channels (stereo)
    int chunk_samples = 1024;              // provider chunk size
};

// Timeline → PCM mixer. UI-free, single consumer thread. Init()
// snapshots the project; decoder sessions open lazily on the first
// chunk overlapping the clip's extended audio range and drain to EOF.
// GetChunk is random-access (the mux may interleave calls) but chunk
// production is sequential internally — callers must request indices
// in non-decreasing order (the mux does).
class AudioMixer {
public:
    AudioMixer();
    ~AudioMixer();
    AudioMixer(const AudioMixer&) = delete;
    AudioMixer& operator=(const AudioMixer&) = delete;

    bool Init(const Project& project, const AudioMixerOptions& options,
              std::string* error_out = nullptr,
              std::vector<std::string>* warnings_out = nullptr);
    void Shutdown();

    double DurationSeconds() const;   // project.duration()
    // ceil(duration * sample_rate / chunk_samples), >= 1 when
    // duration > 0 — the audio_frame_count for the encode calls.
    std::size_t ChunkCount() const;

    // Fills `out` for chunk `index` (media span
    // [index*chunk_samples/rate, min((index+1)*chunk/rate, duration))):
    //   sample_rate=48000, channels=2, planar=false,
    //   sample_format="s16", sample_count = chunk_samples (last chunk
    //   short), data = interleaved signed-16 LE L,R.
    // pts = index*chunk_samples (debug aid — the mux interleaves by
    // cumulative sample_count/sample_rate and ignores pts).
    // Silence (all-zero PCM) where no clip contributes. Returns false
    // + error_out on decode failure; after Cancel() returns false with
    // a cancelled error.
    bool GetChunk(std::size_t index,
                  XER::Bridge::FFmpeg::AudioFrameInfo* out,
                  std::string* error_out = nullptr);

    void Cancel();
    bool Cancelled() const;

    const std::vector<std::string>& Warnings() const { return warnings_; }

private:
    // defined in audio_mixer.cc
};

}  // namespace XER::VideoEditor
```

**Clip eligibility** (computed at Init): clip contributes audio iff
`!clip.muted` AND its track exists AND `!track.muted` AND NOT
(`track.kind == Video && track.hidden`) AND the source probes/decodes
with `HasAudio()`. `track.muted` applies to AUDIO-kind tracks only
(video-track `muted` is ignored per the model comment — "video:
ignored (use hidden)"); `track.hidden` on a video track removes video
AND its embedded audio; `hidden` on audio tracks is ignored.
`clip.gain_db` applies; `track.gain_db` is Phase-2 (ignored — document).

**Extended audio range** = same handle math as video:
`[start - in_handle, end + out_handle)` via `ClipTransitionHandles` —
transition crossfade applies to audio too (below). Audio-track clips
have no transitions → nominal range.

- [ ] **Step 1: Write the failing mixer tests**

Create `tests/video/video_editor_audio_mixer_tests.cc` exposing
`int XER_VideoEditor_RunAudioMixerTests()`. Fake decoder serves
scripted `AudioFrameInfo` chunks (s16 stereo 48 kHz or 44.1 kHz sine —
build them in-test, no FFmpeg). Required cases:

```cpp
// -- gain/mute/fade envelopes (fake decoder, no FFmpeg) ------------
// Single clip 0-1s, constant sine → GetChunk mid-clip: non-zero PCM,
// roughly full-scale*sine; gain_db=-6 → amplitude ~half (+-1dB).
// clip.muted → all-zero chunk. Track muted (audio kind) → zeros.
// fade_in=0.25s → first chunk samples ramp 0→full (check first
//   quarter of chunk 0 is quieter than last quarter).
// fade_out symmetric at clip end.
// Video-track clip with muted TRACK → audio still plays (muted is
//   audio-kind only); video-track track.hidden → silence.
// Audio clip on AUDIO track overlapping a video clip's audio → both
//   contribute (sum can clip at +-full scale — check clamp).
// -- transitions ----------------------------------------------------
// A(0-2s)→B(2-4s) video-track clips, crossfade D=1: chunk covering
//   t=1.75 ≈ all A; t=2.0 ≈ equal mix; t=2.25 ≈ all B. (Both clips'
//   audio must be decoded through their handle regions — the fake
//   decoder must supply frames whose pts cover in_point-D/2 ..
//   out_point+D/2.)
// Wipe/slide transitions: audio still crossfades by weight (geometry
//   is video-only).
// -- chunking/progress ----------------------------------------------
// ChunkCount == ceil(duration*48000/1024); last chunk short
//   (sample_count < 1024); index >= ChunkCount() → false +
//   "chunk index out of range" (pinned — matches the header contract).
//   Every chunk reports identical format fields (the encoder's
//   consistency contract).
// -- resample/format conversion --------------------------------------
// 44.1 kHz s16 stereo source → output is 48 kHz s16 stereo; duration
//   preserved (chunks cover the clip's media span); amplitude sane.
// Mono source → dual-mono output. u8/s32/flt formats → converted
//   without wrap (spot-check a DC-level frame).
// Planar input ("s16p" + plane_sizes) → same result as interleaved.
// -- media-time anchoring / sparse pts -------------------------------
// Fake frames with a pts GAP (silence hole) → output has zeros in the
//   hole, not shifted audio (the gap-fill rule).
// First frame pts > 0 (AAC-priming-like) → audio starts at its media
//   time; clip in_point maps correctly (a clip starting at in_point=X
//   plays the frame whose pts≈X, not frame 0).
// EOF mid-clip → tail is silence, AudioDrained reached, no spin
//   (guard counter in test).
// -- lifecycle --------------------------------------------------------
// Two clips same source far apart → 2 decoder Opens (per-instance).
// Session closes/drains at clip end — subsequent chunks don't re-decode.
// Cancel() mid-mix → GetChunk false + cancelled; Shutdown idempotent.
// -- FFmpeg-gated integration -----------------------------------------
// Real wav fixture (EncodeAudioFrames sine) on an audio track →
//   exported chunk non-zero, duration ≈ clip duration (self-skip).
```

- [ ] **Step 2: Build — verify RED**

`"D:/Developers/XER3/out/vcenv.bat" cmake --build out/build/default --target XER_VideoEditor_Tests` → `audio_mixer.h` not found.

- [ ] **Step 3: Implement `audio_mixer.{h,cc}`**

Session struct + pipeline:

```cpp
struct AudioClipSession {
    const Clip* clip; const Track* track;
    MediaDecoderPtr decoder;
    bool open_attempted=false, open_ok=false, dead=false, eof=false;
    double ext_start=0, ext_end=0;      // contribution window (s)
    // stream format (from first decoded frame — constant per file)
    int src_rate=0, src_channels=0;
    std::string src_fmt; bool src_planar=false;
    double audio_tb=0;                  // GetAudioTimeBase()
    // media-time-indexed stereo float window @src_rate:
    //   src_pcm[0] corresponds to media time `pcm_media_start`.
    std::deque<float> src_pcm;          // interleaved L,R float [-1,1]
    double pcm_media_start=0;           // media s of src_pcm[0]
    double pcm_media_end=0;             // media s one past last sample
    double media_duration=0;            // decoder duration (0 unknown)
    double gain_lin=1.0;                // pow(10, gain_db/20)
    // resample state is implicit: output sample at media time s reads
    // src_pcm at index (s - pcm_media_start)*src_rate (linear interp).
};
```

`Init`: copy project; compute per-clip eligibility + windows
(`ClipTransitionHandles`); store sessions for ALL eligible clips
(lazy open). Warn on clips skipped for muted/hidden (debug-level —
optional, keep quiet: muting is normal).

`GetChunk(index)`:
1. `T0 = index*chunk_samples/sample_rate`, `T1 = min(T0 +
   chunk_samples/rate, duration_)`; frames `n = round((T1-T0)*rate)`
   clamp `<= chunk_samples`.
2. For each session overlapping `[T0,T1)`: `EnsureAudio(session, T1)`
   — pump decoder until `pcm_media_end >= needed_src_end + margin`
   (margin ≈ 0.1 s) or eof/error/cancel:
   ```
   while (!covers && !eof && !cancelled) {
       std::vector<uint8_t> v; int w,h;
       decoder->NextFrame(&v,&w,&h);          // pump (audio-only:
                                            // bounded batch, returns
                                            // false but still pumps)
       std::vector<AudioFrameInfo> af;
       decoder->ReadAudioFrames(&af, 64);     // false w/o codec is OK
       for (auto& f : af) AppendBlock(session, f);
       if (decoder->AudioDrained() && af.empty()) eof=true;
       // ALSO: NextFrame()==false on A/V files means video EOF — but
       // audio may still drain; rely on AudioDrained() + empty reads.
       // Pump guard: max ~20000 iterations/chunk → error "audio decode
       // starvation" (sparse-PTS Review Focus #3).
   }
   ```
   On first `AppendBlock`: capture `src_rate/channels/fmt/planar`,
   `audio_tb`; convert each `AudioFrameInfo` → stereo float @src_rate
   (`ConvertToStereoFloat` helper — see below); media-start of the
   block = `(best_effort_timestamp != INT64_MIN ? bet : pts) * tb`
   (fallback: `pcm_media_end` continuity when pts missing); append to
   `src_pcm` with GAP/OVERLAP handling:
   - `block_start > pcm_media_end + eps` → zero-fill the hole
     (silence — sparse-PTS rule).
   - `block_start < pcm_media_end - eps` → drop the overlapping prefix
     (priming/overlap rule).
   - Trim `src_pcm` front while `pcm_media_start < min_needed -
     margin` (bounded window — anti-goal #5).
3. Per output frame `i` (timeline τ = T0 + i/rate): zero acc[L,R];
   for each overlapping session: if τ in `[ext_start, ext_end)`:
   `src_media = in_point + (τ - timeline_start)` clamp `[0,
   media_duration]`; `pos = (src_media - pcm_media_start)*src_rate`;
   if `0 <= pos < pcm_frames-1` → bilinear interp L,R → `gain(τ)` →
   acc += . `gain(τ)` =
   `gain_lin * FadeEnv(clip, τ) * TransitionWeight(clip, τ, project)`:
   - `FadeEnv`: `fi = fade_in>0 ? clamp((τ-start)/fade_in,0,1) : 1`;
     `fo = fade_out>0 ? clamp((end-τ)/fade_out,0,1) : 1`; product.
     (τ outside [start,end) → the clamps yield 0 when the
     corresponding fade exists — fades do NOT extend into handles;
     document.)
   - `TransitionWeight`: clip is `from` of T → inside window weight
     `1-w_t`; clip is `to` → `w_t`; outside → 1. (Same `w_t` as video —
     find the transition by clip id via a precomputed map.)
4. Convert acc → s16: `clamp(v,-1,1) * 32767` rounded; pack
   interleaved; fill `AudioFrameInfo` fields per the header contract;
   `index >= ChunkCount()` → false + "chunk index out of range".

`ConvertToStereoFloat(AudioFrameInfo → append stereo float @src_rate)`:
- Read `sample_format` ∈ {u8,s16,s32,flt,dbl} (also "…p" planar via
  `planar` flag + `plane_sizes`); normalize to float [-1,1]
  (u8:(v-128)/128, s16:/32768, s32:/2147483648, flt/dbl: raw).
- Downmix at SRC rate: `channels==1` → L=R=ch0; `>=2` → L=ch0,R=ch1
  (5.1 FL,FR are ch0,ch1 — acceptable v1; document); `>2` → first two.
- Interleaved: frame stride = channels; planar: plane c holds channel
  c (`plane_sizes[c]` bytes each — validate sizes).
- Unknown format → session `dead=true` + warning once (skip clip).

- [ ] **Step 4: Build + run — verify GREEN**

Build + run `XER_VideoEditor_Tests`. Expected: mixer suite passes.

- [ ] **Step 5: Commit**

```bash
git add tools/video_editor/audio_mixer.{h,cc} \
        tests/video/video_editor_audio_mixer_tests.cc \
        tests/video/video_editor_test_main.cc tests/CMakeLists.txt
git commit -m "Add VideoEditor audio mixer: PCM chunks for export mux"
```

---

## Task 6: `export_engine`

Background worker: snapshot project → probe sources → pick encode path
→ drive providers → atomic temp→commit → progress/cancel. UI-free;
1D's export dialog wraps it.

**Files:**
- Create: `tools/video_editor/export_engine.h`,
  `tools/video_editor/export_engine.cc`
- Create: `tests/video/video_editor_export_tests.cc`
- Modify: `tests/video/video_editor_test_main.cc`,
  `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `timeline_model`, `compositor`, `audio_mixer`,
  `media_decoder`, `project_store` (not directly — callers load),
  `wrapper/ffmpeg/ffmpeg_engine_bridge.h`,
  `content/video/video_info.h` (`ProbeVideo`),
  `cache/cache_utils.h` (`TempPathForTarget`/`CommitTempFile` —
  transitive via `XER_Video` PUBLIC link; no CMake edit),
  `helper/path_utils.h`.
- Produces (`tools/video_editor/export_engine.h`, namespace
  `XER::VideoEditor`):

```cpp
struct ExportOptions {
    std::string output_path;      // UTF-8; container from extension
                                  // (.mp4 default mux)
    std::string video_codec = "libx264";
    std::string audio_codec = "aac";
    int video_bit_rate = 8000000; // 0 → bridge default (4 Mbps)
    int audio_bit_rate = 128000;  // 0 → bridge default (192k)
    // Canvas/fps come from the Project — not options.
};

enum class ExportStatus { kIdle, kRunning, kDone, kCancelled, kError };

struct ExportProgress {
    double fraction = 0.0;        // [0,1], monotonic non-decreasing
    std::uint64_t video_frames_done = 0, video_frames_total = 0;
    std::uint64_t audio_chunks_done = 0, audio_chunks_total = 0;
    std::string stage = "init";   // init|video|audio|mux|commit
};

// Offline frame-accurate export worker. UI-free; Start() snapshots
// the project and spawns ONE std::thread; all encode work happens
// there. Wait() joins (destructor Cancel+Waits — never leaks a worker
// touching a dead engine).
class ExportEngine {
public:
    ExportEngine();
    ~ExportEngine();
    ExportEngine(const ExportEngine&) = delete;
    ExportEngine& operator=(const ExportEngine&) = delete;

    // Validates + snapshots `project`, probes unique source paths
    // (ProbeVideo) to decide has_video/has_audio, computes frame/chunk
    // counts, spawns the worker. Returns false + error_out on: empty
    // duration, empty output path, FFmpeg unavailable, probe failure
    // (missing media fails FAST — before any encode), or already
    // running. `progress` (optional) is invoked on the worker thread —
    // marshal to UI before touching widgets.
    bool Start(const Project& project, const ExportOptions& options,
               std::function<void(const ExportProgress&)> progress = {},
               std::string* error_out = nullptr);

    void Cancel();        // cooperative; any thread; idempotent
    bool Wait();          // blocks until worker exits; true if joined
    bool IsRunning() const;
    ExportStatus Status() const;
    ExportProgress Progress() const;   // latest snapshot
    const std::string& Error() const;  // meaningful when kError

private:
    // defined in export_engine.cc
};

}  // namespace XER::VideoEditor
```

- [ ] **Step 1: Write the failing export tests**

Create `tests/video/video_editor_export_tests.cc` exposing
`int XER_VideoEditor_RunExportTests()`. FFmpeg-gated (self-skip via
`IsAvailable()`); synthesize fixtures with the bridge encode APIs
(`EncodeVideoFrameStream` tiny clip, `EncodeAudioFrames` wav — patterns
already in `ffmpeg_audio_tap_tests.cc`/`ffmpeg_av_mux_tests.cc`).
Required cases:

```cpp
// -- path selection (the spec's three edges) ------------------------
// video-only project (1 clip on video track, no audio in file):
//   export .mp4 → ProbeMedia: exactly 1 video stream, 0 audio.
//   (EncodeVideoFrameStream path — EncodeAudioVideoStream with
//   audio_frame_count=0 produces the same file; EITHER is acceptable
//   — pin whichever the impl chooses; document.)
// audio-only project (wav on audio track): export .mp4 →
//   ProbeMedia: exactly 1 audio stream, 0 video (EncodeAudioStream).
// A/V project (mp4-with-audio or video clip + wav clip): export →
//   2 streams; decode a few frames both ways sanity.
// -- atomic write ----------------------------------------------------
// Success → target exists, temp file GONE (glob target dir for
//   "*.tmp.*" leftovers).
// Injected failure (project references a MISSING source file —
//   probe fails at Start → error, no file; OR a clip whose decode
//   fails mid-export): target must NOT exist / unchanged, temp gone.
// Cancel mid-export (short project, Cancel() right after Start):
//   status kCancelled, target absent, temp gone. (Timing-sensitive —
//   use a LONG-ish fixture ~10s so cancel lands mid-run; if flaky,
//   cancel from the progress callback at fraction>0.2.)
// CommitTempFile failure path: output_path in a nonexistent dir →
//   Start succeeds (probe ok), worker fails at open/commit → kError,
//   target absent. (avformat_open may fail earlier — accept either
//   kError with non-empty Error().)
// -- progress ---------------------------------------------------------
// Progress fractions observed via callback are non-decreasing and
// reach 1.0 on success; stage transitions init→…→commit.
// -- validation --------------------------------------------------------
// Empty project (no clips) → Start false. duration=0 → false.
// FFmpeg unavailable → false (only assert when !IsAvailable — can't
//   simulate otherwise; document).
// -- history contract --------------------------------------------------
// (Documented, not tested here: CLI loads with no bound history.)
```

- [ ] **Step 2: Build — verify RED**

`"D:/Developers/XER3/out/vcenv.bat" cmake --build out/build/default --target XER_VideoEditor_Tests` → `export_engine.h` not found.

- [ ] **Step 3: Implement `export_engine.{h,cc}`**

Worker `Run()` outline (implement carefully — this is the
cancellation/atomicity core):

```cpp
void ExportEngine::Run() {
    // members: project_ (copy), options_, progress_cb_,
    //          cancel_ (std::atomic<bool>), status_, error_,
    //          progress_ (mutex-guarded snapshot), temp_path_
    SetStage("init");

    // 1. Probes were done in Start(); recompute nothing.
    // 2. temp output — TempPathForTarget(target,"xerexp") + extension
    //    (music_bass_eq_clip.cc:570-571 pattern):
    //      temp = Cache::TempPathForTarget(out, "xerexp");
    //      temp += fs::path(out).extension().string();
    //    fs::remove(temp, ec) best-effort; create parent dirs of temp.
    // 3. Build engines (only what's needed):
    //      has_video → Compositor comp; comp.Init(project_, {}, &err)
    //      has_audio → AudioMixer mix;  mix.Init(project_, {}, &err)
    //    Init failure → Fail(err) (before any file written).
    // 4. Providers (capture comp/mix by pointer; frames reuse buffers):
    //      video_provider(n):
    //        if cancel → false "export cancelled"
    //        t = n * fps_den / fps_num   (double; exact rational→double)
    //        comp.RenderFrameAt(t, &vbuf, &err) → wrap VideoFrameInfo
    //        {width=W,height=H,pixel_format="bgra",
    //         line_sizes={W*4,0,0,0},data=vbuf}
    //        progress: video_frames_done=n+1; Report()
    //      audio_provider(m):
    //        if cancel → false "export cancelled"
    //        mix.GetChunk(m, &ainfo, &err) → progress audio_chunks_done
    // 5. Encode dispatch:
    //      has_video && has_audio → EncodeAudioVideoStream(
    //          {video:{out=temp,...},audio:{...}}, v_total, vp,
    //          a_total, ap, &err)
    //      has_video && !has_audio → EncodeVideoFrameStream(
    //          vparams, v_total, vp, &err)
    //      !has_video && has_audio → EncodeAudioStream(
    //          aparams, a_total, ap, &err)
    //      (!has_video && !has_audio can't happen — Start rejects.)
    // 6. success → Cache::CommitTempFile(temp, output, &cerr)
    //      fail → status kError, error_=cerr; remove(temp) either way
    //      on failure/cancel. On success status kDone, fraction=1.0.
    // 7. Cancel path: cancel_ checked in providers AND inside
    //      RenderFrameAt/GetChunk (both honor Cancel()) → provider
    //      false → encode aborts → remove temp → kCancelled.
}
```

`Start` details:
- Reject `IsRunning()`; empty `output_path`; `project.duration() <=
  0`; `!XER::Bridge::FFmpeg::IsAvailable()` (early, clean error).
- Probe every UNIQUE `clip.source_path` via
  `XER::Content::Video::ProbeVideo(path, &info, &err)` — canonicalize
  with `Helper::Path::NormalizeUtf8Path` first; treat
  `!info.valid`/false return as probe failure → error_out
  `"cannot probe <path>: <err>"` (missing media fails fast).
  Build `path → {has_video, has_audio}` map
  (`has_video = info.width>0 && info.height>0`,
  `has_audio = info.has_audio`; audio-only files probe with
  width/height=0 per `video_info.h:25-26`).
- `has_video` = any clip on a `TrackKind::Video` track, track NOT
  hidden, whose probe `width>0 && height>0`. (clip.muted doesn't
  matter — muted is audio-only.)
- `has_audio` = any clip where `!clip.muted && track exists &&
  !track.muted && !(track.kind==Video && track.hidden) &&
  probe.has_audio`.
- `!has_video && !has_audio` → error "project has no exportable media"
  (e.g. all clips muted, or empty).
- Frame/chunk totals are finalized inside `Run()` AFTER `Init`ing the
  engines: `total_frames = max(1, llround(duration * fps_num /
  fps_den))`; `audio_chunks_total = mix.ChunkCount()` — the mixer's
  own authoritative count (no duplicated formula). `Start` only needs
  has_video/has_audio for validation; the counts feed progress +
  provider frame_count inside the worker.
- Spawn `std::thread(&ExportEngine::Run, this)`; store handle.
- `progress_` updates under a small mutex; `Progress()` snapshots.
- `fraction`: `0.5*(v_done/v_total) + 0.5*(a_done/a_total)` when both
  streams, else the single stream's fraction; init=0, commit/done=1.
  Monotonic because both counters only increase (interleave order may
  make one lap the other — the SUM is still non-decreasing).
- `Wait()`: `if (thread_.joinable()) thread_.join(); return
  status_ != kRunning;` — set status_ kError "worker thread failed to
  start" if `std::thread` ctor throws (it can — wrap in try/catch,
  exceptions OK at this boundary, NOT in render loops).
- `~ExportEngine`: `Cancel(); Wait();` then engines' Shutdown via
  members' destructors.

- [ ] **Step 4: Build + run — verify GREEN**

Build + run `XER_VideoEditor_Tests`. Expected: export suite passes
(FFmpeg cases skip off-Windows).

- [ ] **Step 5: Commit**

```bash
git add tools/video_editor/export_engine.{h,cc} \
        tests/video/video_editor_export_tests.cc \
        tests/video/video_editor_test_main.cc tests/CMakeLists.txt
git commit -m "Add VideoEditor export engine: mux, atomic write, cancel"
```

---

## Task 7: `main.cc` CLI + `video_editor` exe target

Headless CLI per plan index: `video_editor --export in.xerproj
out.mp4`. GUI is 1D — this exe intentionally links only
`XER_VideoEditor_Core` (no `XER_UI_Core`). Pattern follows
`tools/preview/main.cc`: UTF-16 command line → UTF-8 args on Windows,
`--help`, platform fallback message.

**Files:**
- Create: `tools/video_editor/main.cc`
- Modify: `tools/video_editor/CMakeLists.txt` (exclude main.cc from
  lib glob + add exe)

**Interfaces:**
- Consumes: `ProjectStore::Load`, `ExportEngine`, `ProbeVideo`
  (for `--info` media summary), `Helper::Path`.
- Produces: `video_editor.exe` in `out/build/default/bin`.

- [ ] **Step 1: Write `main.cc`**

```cpp
// video_editor.exe — XER Studio Phase 1C headless CLI.
//   video_editor --export <in.xerproj> <out.mp4>   render+encode
//   video_editor --info <in.xerproj>               project summary
//   video_editor --help
// GUI (Cut workspace) lands in Phase 1D on this same exe target.
```

Implementation requirements:
- Windows: reuse the preview pattern — `SetConsoleOutputCP(CP_UTF8)`,
  `CommandLineToArgvW` → `WideCharToMultiByte(CP_UTF8)` per arg
  (copy the `WideArgToUtf8`/`CommandLineArgsUtf8` helpers verbatim
  from `tools/preview/main.cc:51-76` — they're file-local, so
  duplicate ~25 lines; do NOT try to share across tool dirs).
- `--export`: `ProjectStore::Load(in, &project, &err)` → NOTE in a
  comment: "no TimelineHistory is bound in CLI mode — the
  history.Clear()-before-load contract is trivially satisfied; 1D's
  dialog owns the history-bound path." → `ExportOptions{output_path=
  out}` → `ExportEngine::Start(project, opts, progress_cb)` where
  progress_cb prints `"\r  export: <pct>% (<stage>)"` throttled to
  ≥100 ms intervals (or every fraction*100 change) → `Wait()` →
  switch on `Status()`: kDone → newline + `"exported <frames> frames
  -> <out>"` exit 0; kCancelled → `"export cancelled"` exit 2;
  kError → `"export failed: <Error()>"` exit 1.
- `--info`: load project → print canvas (WxH, fps_num/fps_den,
  bg_color), duration, track list (kind/name/muted/hidden), clip count
  per track, transition count; then for each unique source_path print
  `ProbeVideo` summary (w/h/fps/duration/has_audio) or "probe failed:
  <err>". Off-Windows `ProbeVideo` fails → print model data + note.
- No args / unknown flag / missing operands → usage, exit 1.
- Non-Windows: `--export` reaches `ExportEngine::Start` →
  `IsAvailable()` false → clean "FFmpeg bridge unavailable" error;
  `--info` works model-only. No `#if _WIN32` GUI stubs needed (there
  is no GUI yet) — keep the file platform-clean like preview's CLI
  half.

- [ ] **Step 2: CMake wiring**

`tools/video_editor/CMakeLists.txt` — TWO edits:

```cmake
# 1. Exclude the exe's main() from the core lib glob (it must not be
#    compiled into XER_VideoEditor_Core):
list(FILTER VIDEO_EDITOR_CORE_SOURCES EXCLUDE REGEX "/main\\.cc$")

# 2. After the existing target_compile_options block, append:

# video_editor.exe — Phase 1C headless CLI (--export/--info). GUI
# sources join THIS target in 1D; it intentionally does not link
# XER_UI_Core yet. RUNTIME_OUTPUT_DIRECTORY matches the global bin/.
add_executable(video_editor main.cc)
target_link_libraries(video_editor PRIVATE XER_VideoEditor_Core)
target_compile_features(video_editor PRIVATE cxx_std_23)
set_target_properties(video_editor PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(MSVC)
    target_compile_options(video_editor PRIVATE /W4 /WX- /O2)
else()
    target_compile_options(video_editor PRIVATE -Wall -Wextra -Wpedantic -O3)
endif()
```

- [ ] **Step 3: Build + smoke**

`"D:/Developers/XER3/out/vcenv.bat" cmake --preset default` (new target)
then `"D:/Developers/XER3/out/vcenv.bat" cmake --build out/build/default --target video_editor`
Smoke: `out/build/default/bin/video_editor.exe --help` → usage;
`--info` on a nonexistent .xerproj → clean load error, exit 1.
(Do NOT commit binaries.)

- [ ] **Step 4: Commit**

```bash
git add tools/video_editor/main.cc tools/video_editor/CMakeLists.txt
git commit -m "Add video_editor CLI with --export/--info (Phase 1C)"
```

---

## Task 8: Full verification & wrap-up

**Files:** none new — verification only.

- [ ] **Step 1: Rebuild everything affected**

Run: `"D:/Developers/XER3/out/vcenv.bat" cmake --build out/build/default --target video_editor XER_VideoEditor_Tests XER_Video_Smoke XER_UI_Tests XER_ContentVideo`
Expected: zero warnings (`/W4`), clean build — including the
`EncodeAudioStream` + decoder/skia fixes from Tasks 1-3.

- [ ] **Step 2: Run the full test suite**

Run: `"D:/Developers/XER3/out/vcenv.bat" ctest --test-dir out/build/default --output-on-failure`
Expected: `XER_VideoEditor_Tests` (all EIGHT suites:
model/history/store/catalog/poster/decoder/compositor/audio_mixer/
export), `XER_Video_Smoke` (strict blend values), `XER_UI_Tests`
(incl. new `EncodeAudioStream` + seek-position cases), existing suites
pass. Spot-run `out/build/default/bin/XER_VideoEditor_Tests.exe`
directly for per-check output.

- [ ] **Step 3: Real end-to-end export smoke (manual, FFmpeg-gated)**

Synthesize a tiny project by hand OR via a scratch .xerproj: one
clip with audio + one audio-only clip + a crossfade → run
`video_editor --export probe.xerproj out.mp4` → `ProbeMedia` shows 2
streams; play/inspect duration ≈ project duration. Document result in
the commit/report (this is the spec's "export→plays" smoke seed for
1E).

- [ ] **Step 4: Final review pass**

- `git status` clean except intended files; `git log` shows the task
  commits on top of `b966af9`.
- Confirm no FFmpeg headers leaked: `grep -rn "libav\|libsw\|libyuv"
  tools/video_editor/ tests/video/` → comments/wrapper-includes only.
- Confirm `main.cc` is NOT in `XER_VideoEditor_Core` (check
  `cmake --build` object list or `dumpbin`/`lib` contents if unsure —
  simplest: `grep -n "main" tools/video_editor/CMakeLists.txt` shows
  the FILTER line).
- Confirm `EncodeAudioStream` mirrors `EncodeAudioFrames` epilogue
  (packet free, pb close, state release, trailer ordering).
- Update `docs/wiki/architecture/video-pipeline-facts.md` — add a
  "Phase-1C additions" section: `EncodeAudioStream`,
  `IMediaDecoder`/`BridgeVideoDecoder`, Compositor/AudioMixer/
  ExportEngine contracts, the two fixed bugs (Seek rescale, BlendSrcOver
  — REMOVE them from "Known issues FIX-SOON"), remaining handoffs
  (AAC priming alignment verification, track/project-setting mutators
  → 1D, `position_seconds` audio-only → mixer uses pts*tb already).
- `docs/` is untracked in this repo — leave untracked unless asked.

---

## Spec gaps resolved in this plan (flag if you disagree)

- **`EncodeAudioStream` added to the bridge** (spec said audio-only →
  `EncodeAudioFrames`): that API is batch-only and would materialize
  the entire mix as `vector<AudioFrameInfo>` (~700 MB for 30 min
  stereo) — violates the "audio streams PCM chunks" anti-goal. The new
  API is ~120 lines reusing the anonymous-namespace encoder helpers
  built for composition; symmetric with `EncodeVideoFrameStream`.
  Fallback if rejected: `EncodeAudioFrames` with documented memory cap.
- **`IMediaDecoder` abstraction** (spec implied direct
  `VideoDecoder` use): the concrete decoder can't be faked, and ALL
  compositor/mixer logic (transitions, transforms, PTS filter, gain
  envelopes, gap-fill) needs unit tests without FFmpeg. Adapter is a
  thin pass-through; production path unchanged.
- **Compositor owns affine + transitions itself** (spec allowed
  "extend CompositingEngine or keep own layers"): `CompositingEngine::
  Layer` has only int x/y + opacity — no scale/rotation; own inverse-
  mapped bilinear warp + per-pixel transition math in CPU BGRA. No
  Skia/GPU in the hot path (Skia filters only inside `VideoEffects`,
  which are optional per-clip).
- **Effect set = full `VideoEffects` dispatch table** (13 types):
  CPU-safe types always run; Skia-only types (sharpen, edge_detect,
  emboss, vignette, chromatic_aberration, pixelate, oil_paint,
  motion_blur) run on Windows (Skia always linked via `XER_Video`)
  and warn+skip when `!IsAvailable()`; unknown types warn+skip.
  `gaussian_blur` maps to `GaussianBlur` (BoxBlur CPU fallback).
- **Wipe/slide conventions fixed**: wipe reveals incoming from the
  named edge (WipeLeft = from left, growing right); Slide<D> = BOTH
  layers translate toward <D> (under exits, over enters — Premiere-
  style). Hard edges, no feather in v1.
- **bg_color = {r,g,b,a}** — spec/store don't pin channel order;
  compositor interprets [0]=R..[3]=A (matches `Canvas::Clear(r,g,b,a)`
  convention). Only affects non-symmetric colors.
- **Track semantics**: `track.muted` silences AUDIO-kind tracks only
  (model comment: "video: ignored"); `track.hidden` on video tracks
  removes video+embedded audio; `hidden` on audio tracks ignored;
  `clip.muted` = audio-only mute (video still composites — Premiere
  semantics per spec); `track.gain_db` ignored (Phase 2).
- **No new model mutators**: 1C only READS persisted track/project
  fields; writers for canvas/fps/bg_color/track properties deferred
  to 1D inspector (recommended scope — export correctness doesn't
  require them).
- **Mutator/history**: export snapshots `Project` by value at
  Init/Start — no `TimelineHistory` interaction; CLI has no history
  (contract trivially satisfied, documented in main.cc).
- **Audio does not "freeze"**: video EOF repeats the last frame
  (spec's edge-freeze); audio past media end = silence (repeating PCM
  would be a DC offset). Both clamp to file bounds — never read
  outside.
- **Resampling = on-demand linear interpolation** evaluated in media-
  time domain (no streaming resampler state): matches repo precedent
  (`video_audio_player.cc:34-37` accepts linear-interp aliasing);
  `AudioSampleRateConverter` rejected — its block API resets phase
  per call (no `buffer_pos_` use in `Convert`) → chunk-boundary
  discontinuities.
- **Downmix >2ch → first two channels** (5.1 FL,FR = ch0,ch1 — correct
  for the common case); proper downmix matrix is Phase-2 polish.
- **Exe shape**: `video_editor` exe inside `tools/video_editor/
  CMakeLists.txt` linking `XER_VideoEditor_Core` PRIVATE — matches the
  preview pattern, keeps 1D GUI sources on the same target, zero root
  CMakeLists churn. `main.cc` excluded from the core glob via
  `list(FILTER ... EXCLUDE REGEX)`.
- **Progress = mean of stream fractions** — monotonic, simple;
  stage string carries phase detail for the UI.
- **`total_frames`/`audio_chunks` rounding**: `llround(duration*fps)`
  / `ceil(duration*rate/chunk)` — sub-frame tails round to nearest;
  mux interleave tolerates the ≤half-frame A/V tail mismatch.
- **Test placement**: new suites in `tests/video/` on the existing
  `XER_VideoEditor_Tests` plain-assert harness (8 suites total);
  bridge-level tests stay in `tests/ui/ffmpeg_*` (XER_UI_TEST).
