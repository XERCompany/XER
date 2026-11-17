# Phase 1A — Engine Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use subagent-driven-development or executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Prove `XER_Video` links as a real consumer, relax
`Bridge::FFmpeg::VideoDecoder::Open` for audio-only containers, and add
the `EncodeAudioVideoStream` two-stream mux API — the three engine
primitives Phase-1B/C/D all stand on.
**Architecture:** Three engine-surface changes under `src/` exercised by
tests only (no editor UI): (1) a new `XER_Video_Smoke` test exe links
`XER_Video` and exercises `video::Frame`/`MixSystem`/`VideoEffects`/
`FrameExporter`; (2) `VideoDecoder` gains an audio-only mode — `Open`
succeeds without a video stream, `NextFrame` pumps the demuxer for the
audio tap and always returns false; (3) shared encoder internals are
extracted from `EncodeVideoFrameStream`/`EncodeAudioFrames` and reused
by the new `EncodeAudioVideoStream` mux (one `AVFormatContext`, video +
audio streams, PTS-interleaved writes).
**Tech Stack:** C++23, CMake + Ninja (preset `default`, vcpkg
`x64-windows-release`, MSVC), FFmpeg bridge (`libav*`/`libyuv` confined
to `src/wrapper/ffmpeg`), plain-assert test style.
**Spec:** docs/superpowers/specs/2026-11-15-video-editor-design.md (and
docs/wiki/architecture/video-pipeline-facts.md for verified API facts)

## Global Constraints

- **Windows/MSVC only** for anything touching the video path: FFmpeg
  bridge symbols are defined only `if(MSVC)`
  (`src/wrapper/ffmpeg/CMakeLists.txt:45-53`) and decoder bodies are
  `#ifdef _WIN32`. Tests must self-skip via
  `XER::Bridge::FFmpeg::IsAvailable()` like
  `tests/ui/ffmpeg_video_decode_tests.cc` does.
- **C++23**, repo style: snake_case files, camelCase/PascalCase code,
  tabs, `#pragma once`, no exceptions in hot paths. The bridge .cc uses
  **manual cleanup** (no RAII guards) — match it.
- **No FFmpeg headers outside `src/wrapper/ffmpeg`**: tests and future
  consumers include only `wrapper/ffmpeg/ffmpeg_engine_bridge.h` and
  `wrapper/ffmpeg/ffmpeg_video_decoder.h`.
- Do **not** touch the mock functions (`RunFilterGraph`, `Transcode`,
  `CaptureDeviceToStream`).
- TDD where infrastructure exists: `XER_UI_TEST`/`XER_UI_EXPECT_*`
  harness for bridge tests; plain-assert `main()` for the `XER_Video`
  smoke exe (matches `tests/dependencies_smoke.cc`).
- Build dir: `out/build/default` (Ninja preset). The VS-generator tree
  `build/` also works — commands below use the preset tree; for `build/`
  substitute `cmake --build build --config Release --target <t>`.
- **MSVC env required**: every `cmake --build`/`ctest` invocation must
  run under vcvars or it fails with `C1083` on standard headers:
  `cmd //c "call \"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat\" >nul && cmake --build --preset default"`

## Review Focus

The five input classes / failure modes most likely to bite:

1. **Audio-only container with no audio stream** (data/subtitle-only
   file): `Open` must still fail cleanly with `error_out` set. Also an
   A/V file whose *video* codec fails to open must not leave
   `IsOpen() == true` — `has_audio` is now probed before the video
   codec succeeds, so `IsOpen` must key on opened objects, not flags.
2. **Mux with zero audio frames** (`audio_frame_count == 0`) and
   **provider failure mid-stream**: zero audio must yield a valid
   video-only file (no empty audio stream); a provider returning false
   must propagate the error, skip `av_write_trailer` only when the
   header was never written, and free every allocated object.
3. **VFR / irregular audio chunks**: the last chunk is typically short
   (e.g. 640 of 1024 samples) and every chunk's `sample_rate`/`channels`/
   `sample_format` must match the encoder state or `FeedAudioEncoderFrame`
   errors — the interleave clock uses per-chunk `sample_count /
   sample_rate`, never a fixed chunk duration.
4. **Encoder delay / priming**: libx264 B-frames and AAC priming emit
   packets out of input order; `av_interleaved_write_frame` must see
   packets from *both* streams before `av_write_trailer`, and both
   encoders must be fully flushed. Tests assert decoded frame/audio
   counts (tolerant), not exact PTS.
5. **`Open` on a corrupt/truncated file** and **audio-tap starvation**:
   `avformat_open_input`/`avformat_find_stream_info` failures must set
   `error_out` and leave `Close()`/destructor safe; for audio-only
   files the tap only fills while `NextFrame()` pumps — the pump must be
   bounded per call and the consumer contract (`NextFrame()` +
   `ReadAudioFrames()` until `AudioDrained()`) documented in the header.

---

## Task 1: XER_Video link smoke test (`XER_Video_Smoke`)

**Files:**
- Create: `tests/video/xer_video_smoke.cc`
- Modify: `tests/CMakeLists.txt`
- Possibly modify (only if the link fails): `src/video/CMakeLists.txt`
  or offending `src/video/*.cc` — expected orphan-lib fixes, see step 3.

**Interfaces:**
- Consumes: `video::Frame`, `video::PixelFormat`, `video::MixSystem`,
  `video::VideoEffects`, `video::FrameExporter` (all in `src/video`,
  namespace `video`); `XER::Bridge::FFmpeg::{IsAvailable, ProbeMedia,
  MediaInfo}` transitively via `XER_Video` → `XER_FFmpeg`.
- Produces: `XER_Video_Smoke` ctest target — the first binary that links
  `XER_Video`; proves the lib is no longer an orphan and gives 1B–1E a
  green baseline.

- [ ] **Step 1: Write the test file**

Create `tests/video/xer_video_smoke.cc`:

```cpp
// Smoke test proving XER_Video links and its core CPU-side classes
// work: video::Frame, MixSystem, VideoEffects (CPU paths) and
// FrameExporter round-trips through the FFmpeg bridge. Plain-assert
// style (matches tests/dependencies_smoke.cc); process exit code is
// the failure count. FFmpeg-backed checks skip when the bridge reports
// unavailable.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "frame.h"
#include "frame_exporter.h"
#include "mix_system.h"
#include "video_effects.h"
#include "wrapper/ffmpeg/ffmpeg_engine_bridge.h"

namespace {

int g_failures = 0;

void Check(bool condition, const char* expression, const char* file, int line) {
    if (condition) return;
    ++g_failures;
    std::fprintf(stderr, "%s:%d: check failed: %s\n", file, line, expression);
}

#define XER_VIDEO_CHECK(cond) \
    Check(static_cast<bool>(cond), #cond, __FILE__, __LINE__)

std::filesystem::path TempDir() {
    const auto dir = std::filesystem::temp_directory_path() / "xer_video_smoke";
    std::filesystem::create_directories(dir);
    return dir;
}

// Solid-color BGRA8 frame. In-memory channel order is B,G,R,A.
video::Frame MakeSolidFrame(int w, int h, std::uint8_t b, std::uint8_t g,
                            std::uint8_t r, std::uint8_t a) {
    video::Frame frame;
    frame.Allocate(w, h, video::PixelFormat::BGRA8);
    for (int i = 0; i < w * h; ++i) {
        std::uint8_t* p = frame.Data() + static_cast<size_t>(i) * 4u;
        p[0] = b;
        p[1] = g;
        p[2] = r;
        p[3] = a;
    }
    return frame;
}

std::uint8_t Channel(const video::Frame& frame, int x, int y, int channel) {
    return frame.Data()[(static_cast<size_t>(y) * frame.Width() + x) * 4u +
                        static_cast<size_t>(channel)];
}

}  // namespace

int main() {
    // -- video::Frame basics -------------------------------------------
    {
        video::Frame frame;
        XER_VIDEO_CHECK(frame.Allocate(16, 8, video::PixelFormat::BGRA8));
        XER_VIDEO_CHECK(frame.IsValid());
        XER_VIDEO_CHECK(frame.Width() == 16);
        XER_VIDEO_CHECK(frame.Height() == 8);
        XER_VIDEO_CHECK(frame.Format() == video::PixelFormat::BGRA8);
        XER_VIDEO_CHECK(frame.Stride() == 64);           // 16 px * 4 B
        XER_VIDEO_CHECK(frame.DataSize() == 16u * 8u * 4u);
    }

    // -- VideoEffects CPU paths ----------------------------------------
    {
        // BrightnessContrast is pure CPU with exact coefficients:
        // out = clamp((px - 128) * contrast + 128 + brightness).
        video::Frame f = MakeSolidFrame(4, 4, 10, 20, 30, 255);
        video::VideoEffects::BrightnessContrast(f, 10, 1.0f);
        XER_VIDEO_CHECK(Channel(f, 0, 0, 0) == 20);  // B: 10 + 10
        XER_VIDEO_CHECK(Channel(f, 0, 0, 1) == 30);  // G: 20 + 10
        XER_VIDEO_CHECK(Channel(f, 0, 0, 2) == 40);  // R: 30 + 10

        // Grayscale forces R=G=B=luma on both the Skia and CPU paths.
        video::Frame g = MakeSolidFrame(4, 4, 10, 20, 30, 255);
        video::VideoEffects::Grayscale(g);
        XER_VIDEO_CHECK(Channel(g, 0, 0, 0) == Channel(g, 0, 0, 1));
        XER_VIDEO_CHECK(Channel(g, 0, 0, 1) == Channel(g, 0, 0, 2));

        // FlipH mirrors pixels horizontally (pure CPU, no Skia).
        video::Frame h(3, 1, video::PixelFormat::BGRA8);
        auto set_px = [&h](int x, std::uint8_t b, std::uint8_t g,
                           std::uint8_t r) {
            std::uint8_t* p = h.Data() + static_cast<size_t>(x) * 4u;
            p[0] = b; p[1] = g; p[2] = r; p[3] = 255;
        };
        set_px(0, 0, 0, 255);    // left  = red
        set_px(1, 0, 255, 0);    // mid   = green
        set_px(2, 255, 0, 0);    // right = blue
        video::VideoEffects::FlipH(h);
        XER_VIDEO_CHECK(Channel(h, 0, 0, 0) == 255);  // blue at front
        XER_VIDEO_CHECK(Channel(h, 1, 0, 1) == 255);  // green stays center
        XER_VIDEO_CHECK(Channel(h, 2, 0, 2) == 255);  // red at back
    }

    // -- MixSystem -----------------------------------------------------
    {
        video::Frame dst = MakeSolidFrame(4, 4, 10, 20, 30, 255);
        video::Frame src = MakeSolidFrame(4, 4, 200, 100, 50, 255);
        video::MixSystem mix;
        XER_VIDEO_CHECK(mix.Mix(dst, src, video::MixMode::Alpha, 0.5f));
        // CPU path: dst*(1-a) + src*a with a = 0.5 → ~(105,60,40).
        // The Skia src_over path lands within a few LSBs — tolerate it.
        XER_VIDEO_CHECK(std::abs(static_cast<int>(Channel(dst, 0, 0, 0)) - 105) <= 4);
        XER_VIDEO_CHECK(std::abs(static_cast<int>(Channel(dst, 0, 0, 1)) - 60) <= 4);
        XER_VIDEO_CHECK(std::abs(static_cast<int>(Channel(dst, 0, 0, 2)) - 40) <= 4);

        video::Frame a = MakeSolidFrame(4, 4, 0, 0, 0, 255);
        video::Frame b = MakeSolidFrame(4, 4, 100, 150, 200, 255);
        auto blended = mix.Blend(a, b, video::MixMode::Alpha, 0.25f);
        XER_VIDEO_CHECK(blended != nullptr);
        if (blended != nullptr) {
            XER_VIDEO_CHECK(blended->IsValid());
            XER_VIDEO_CHECK(blended->Width() == 4);
            XER_VIDEO_CHECK(blended->Height() == 4);
            // black under 25% of (100,150,200) → B ≈ 25.
            XER_VIDEO_CHECK(std::abs(static_cast<int>(Channel(*blended, 0, 0, 0)) - 25) <= 4);
        }
    }

    // -- FrameExporter round-trips through the FFmpeg bridge ------------
    if (!XER::Bridge::FFmpeg::IsAvailable()) {
        std::fprintf(stderr,
                     "note: FFmpeg bridge unavailable — exporter checks skipped\n");
    } else {
        const auto dir = TempDir();

        // ToFile: single BGRA frame -> PNG on disk.
        const auto png_path = dir / "smoke_frame.png";
        {
            video::FrameExporter exporter;
            video::Frame frame = MakeSolidFrame(32, 24, 10, 200, 30, 255);
            XER_VIDEO_CHECK(exporter.ToFile(png_path.string(), frame));
        }
        XER_VIDEO_CHECK(std::filesystem::exists(png_path) &&
                        std::filesystem::file_size(png_path) > 16);
        if (std::filesystem::exists(png_path)) {
            std::ifstream in(png_path, std::ios::binary);
            char magic[4] = {};
            in.read(magic, 4);
            XER_VIDEO_CHECK(magic[0] == '\x89' && magic[1] == 'P' &&
                            magic[2] == 'N' && magic[3] == 'G');
        }

        // ExportFrameStream: N BGRA frames -> .mp4 -> ProbeMedia sanity.
        const auto mp4_path = dir / "smoke_export.mp4";
        bool streamed = false;
        for (const char* codec : {"libx264", "mpeg4"}) {
            video::FrameExporter exporter;
            streamed = exporter.ExportFrameStream(
                mp4_path.string(), 12, 64, 48, codec, 12,
                [](std::size_t index, std::shared_ptr<video::Frame>* out,
                   std::string*) {
                    *out = std::make_shared<video::Frame>(MakeSolidFrame(
                        64, 48, static_cast<std::uint8_t>(index * 20),
                        100, 200, 255));
                    return true;
                });
            if (streamed) break;
        }
        XER_VIDEO_CHECK(streamed);
        if (streamed) {
            XER::Bridge::FFmpeg::MediaInfo info;
            std::string err;
            XER_VIDEO_CHECK(XER::Bridge::FFmpeg::ProbeMedia(
                mp4_path.string(), &info, &err));
            XER_VIDEO_CHECK(info.stream_count >= 1);
            bool has_video = false;
            for (const auto& s : info.streams) {
                if (s.media_type == "video") has_video = true;
            }
            XER_VIDEO_CHECK(has_video);
        }
    }

    if (g_failures == 0) {
        std::printf("xer_video_smoke: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "xer_video_smoke: %d check(s) failed\n", g_failures);
    return g_failures;
}
```

- [ ] **Step 2: Register the target in `tests/CMakeLists.txt`**

Append at the end of `tests/CMakeLists.txt`:

```cmake

# XER_Video smoke — first real consumer of the (formerly orphan) video
# pipeline lib. MSVC-gated like the rest of the video path; the test
# itself also skips FFmpeg-backed checks when the bridge is stubbed.
if(MSVC)
    add_executable(XER_Video_Smoke video/xer_video_smoke.cc)
    target_link_libraries(XER_Video_Smoke PRIVATE XER_Video)
    target_compile_features(XER_Video_Smoke PRIVATE cxx_std_23)
    add_test(NAME XER_Video_Smoke COMMAND XER_Video_Smoke)
endif()
```

`XER_Video`'s PUBLIC links (`XER_FFmpeg`, `XER_AudioCore`, `XER_Cache`,
`XER_Helper`, `XER_Flux_Core`, `absl`, `fmt`, `range-v3`, GPU bridges)
provide all transitive includes/libs; the global
`CMAKE_RUNTIME_OUTPUT_DIRECTORY=${CMAKE_BINARY_DIR}/bin` puts the exe
next to the FFmpeg DLLs. No `target_include_directories` needed —
`"frame.h"` resolves via `XER_Video` PUBLIC include `src/video`, and
`"wrapper/ffmpeg/..."` via `XER_FFmpeg` PUBLIC include `src`.

- [ ] **Step 3: Build — expect possible orphan-lib link failures**

Run: `cmake --build out/build/default --target XER_Video_Smoke`
(If the build tree was never configured: `cmake --preset default` first.)

Expected outcomes:
- **Links clean** → continue to step 4.
- **Link errors** (unresolved `XER::Bridge::Skia::*`, zlib, or other
  symbols): this is the anticipated orphan-lib work. Likely fixes, in
  order of probability:
  1. `video_effects.cc`/`mix_system.cc`/etc. call
     `XER::Bridge::Skia::*` unconditionally — requires `XER_BridgeSkia`,
     which `src/video/CMakeLists.txt` already PUBLIC-links when
     `unofficial::skia::skia` exists (it does on this machine). If the
     configure lacks the port, add a stub or guard calls with
     `XER_VIDEO_HAS_SKIA`.
  2. Missing dep in `src/video/CMakeLists.txt` `target_link_libraries`
     (e.g. `ZLIB::ZLIB` for `clip_frame_cache.cc`'s `compress2` — note
     `XER_Cache` already PUBLIC-links it, so this should resolve
     transitively).
  3. A `.cc` referencing headers outside `src/video`'s include set —
     add the owning target's PUBLIC link or fix the include.
  Fix minimally; do NOT modify `src/video` renderers
  (CUDA/Vulkan/ANGLE/Skia backends stay untouched per spec anti-goals).

- [ ] **Step 4: Run the smoke test**

Run: `out/build/default/bin/XER_Video_Smoke.exe`
Expected: `xer_video_smoke: all checks passed`, exit code 0.
(FFmpeg checks run for real here — `IsAvailable()` is true on Windows.)

- [ ] **Step 5: Commit**

```bash
git add tests/video/xer_video_smoke.cc tests/CMakeLists.txt
git commit -m "Add XER_Video_Smoke test proving the orphan video lib links"
# include src/video fixes in the same commit if step 3 required any
```

---

## Task 2: Audio-only `VideoDecoder::Open` relaxation

**Files:**
- Modify: `src/wrapper/ffmpeg/ffmpeg_video_decoder.cc`,
  `src/wrapper/ffmpeg/ffmpeg_video_decoder.h` (doc comment only)
- Create: `tests/ui/ffmpeg_audio_tap_tests.cc`
- Modify: `tests/CMakeLists.txt` (add the new file to `XER_UI_Tests`)

**Interfaces:**
- Consumes: `EncodeAudioFrames` + `EncodeAudioParams` +
  `AudioFrameInfo` (fixture generation — pre-existing),
  `VideoDecoder::{EnableAudioTap, Open, IsOpen, HasAudio, GetWidth,
  GetHeight, NextFrame, Seek, ReadAudioFrames, AudioDrained}`.
- Produces: audio-only-capable `VideoDecoder` — `Open` succeeds on
  containers with audio-but-no-video; `IsOpen()`/`HasAudio()` true,
  `GetWidth()/GetHeight()` return 0, `NextFrame()` always returns false
  but keeps pumping the demuxer so `ReadAudioFrames()` keeps receiving
  packets; `Seek()` works (needed by 1C export's `Seek(in_point)`).

- [ ] **Step 1: Write the failing tests**

Create `tests/ui/ffmpeg_audio_tap_tests.cc`:

```cpp
// Tests for VideoDecoder's audio-only container mode: Open() must
// succeed on files with an audio stream but no video stream, the audio
// tap must keep delivering PCM while NextFrame() pumps the demuxer,
// and Seek() must keep working. Fixtures are generated in-test via
// EncodeAudioFrames (WAV/pcm is deterministic; M4A/AAC skips when the
// encoder is absent).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "test_harness.h"
#include "wrapper/ffmpeg/ffmpeg_engine_bridge.h"
#include "wrapper/ffmpeg/ffmpeg_video_decoder.h"

namespace {

constexpr int kSampleRate = 48000;
constexpr int kChannels = 2;

std::filesystem::path TempDir() {
    const auto dir = std::filesystem::temp_directory_path() /
                     "xer_ui_tests_audio";
    std::filesystem::create_directories(dir);
    return dir;
}

// Interleaved signed-16 PCM sine (440 Hz). `first_sample` is the absolute
// sample index so consecutive chunks stay phase-continuous.
void FillSine(XER::Bridge::FFmpeg::AudioFrameInfo* frame,
              std::size_t first_sample, int sample_count) {
    frame->sample_rate = kSampleRate;
    frame->channels = kChannels;
    frame->sample_count = sample_count;
    frame->planar = false;
    frame->sample_format = "s16";
    frame->data.assign(
        static_cast<std::size_t>(sample_count) * kChannels * 2u, 0);
    auto* pcm = reinterpret_cast<std::int16_t*>(frame->data.data());
    for (int i = 0; i < sample_count; ++i) {
        const double t =
            static_cast<double>(first_sample + i) / kSampleRate;
        const std::int16_t v = static_cast<std::int16_t>(
            12000.0 * std::sin(2.0 * 3.14159265358979323846 * 440.0 * t));
        pcm[i * kChannels + 0] = v;
        pcm[i * kChannels + 1] = v;
    }
}

// `total_samples` of sine split into <=1024-sample chunks; the last
// chunk is short — exercises variable sample_count handling.
std::vector<XER::Bridge::FFmpeg::AudioFrameInfo> MakeSineFrames(
    int total_samples) {
    std::vector<XER::Bridge::FFmpeg::AudioFrameInfo> frames;
    for (int done = 0; done < total_samples;) {
        const int n = std::min(1024, total_samples - done);
        XER::Bridge::FFmpeg::AudioFrameInfo f;
        FillSine(&f, static_cast<std::size_t>(done), n);
        frames.push_back(std::move(f));
        done += n;
    }
    return frames;
}

// Audio-only decode driver: NextFrame() returns false (no video) but
// must still be called — it pumps the demuxer so the tap queue fills.
// Returns total decoded samples, or -1 on a hard ReadAudioFrames error.
long long DrainAudio(XER::Bridge::FFmpeg::VideoDecoder* decoder) {
    std::vector<std::uint8_t> bgra;
    int w = 0;
    int h = 0;
    long long total = 0;
    int guard = 0;
    while (!decoder->AudioDrained() && guard++ < 200000) {
        decoder->NextFrame(bgra, w, h);  // pump demuxer / feed tap
        std::vector<XER::Bridge::FFmpeg::AudioFrameInfo> chunk;
        if (!decoder->ReadAudioFrames(&chunk, 256)) {
            return -1;
        }
        for (const auto& f : chunk) {
            total += f.sample_count;
        }
    }
    return decoder->AudioDrained() ? total : -1;
}

}  // namespace

XER_UI_TEST(ffmpeg_decoder_audio_only_wav) {
    if (!XER::Bridge::FFmpeg::IsAvailable()) {
        return;  // FFmpeg is optional — skip rather than fail.
    }
    const auto path = (TempDir() / "audio_only.wav").string();
    std::string err;

    // ~2 s stereo 48 kHz s16 WAV — pcm_s16le needs no optional encoder.
    XER::Bridge::FFmpeg::EncodeAudioParams params;
    params.output_path = path;
    params.codec_name = "pcm_s16le";
    params.sample_rate = kSampleRate;
    params.channels = kChannels;
    params.sample_format = "s16";
    const auto frames = MakeSineFrames(kSampleRate * 2);
    if (!XER::Bridge::FFmpeg::EncodeAudioFrames(params, frames, &err)) {
        std::cerr << "wav fixture encode failed: " << err << '\n';
        XER_UI_EXPECT_TRUE(false);
        return;
    }

    XER::Bridge::FFmpeg::VideoDecoder decoder;
    decoder.EnableAudioTap();
    XER_UI_EXPECT_TRUE(decoder.Open(path, &err));
    if (!decoder.IsOpen()) return;
    XER_UI_EXPECT_TRUE(decoder.HasAudio());
    XER_UI_EXPECT_EQ(decoder.GetWidth(), 0);
    XER_UI_EXPECT_EQ(decoder.GetHeight(), 0);

    // No video stream: NextFrame reports false — but still pumps.
    std::vector<std::uint8_t> bgra;
    int w = 0;
    int h = 0;
    XER_UI_EXPECT_FALSE(decoder.NextFrame(bgra, w, h));

    const long long total = DrainAudio(&decoder);
    XER_UI_EXPECT_TRUE(total > 70000);  // ~2 s @ 48 kHz = 96000 samples
    XER_UI_EXPECT_TRUE(decoder.AudioDrained());

    // Seek must keep working on audio-only files (export seeks to
    // clip.in_point) and the tap must survive it.
    XER_UI_EXPECT_TRUE(decoder.Seek(0.5));
    const long long total2 = DrainAudio(&decoder);
    XER_UI_EXPECT_TRUE(total2 > 20000);
    XER_UI_EXPECT_TRUE(decoder.AudioDrained());
}

XER_UI_TEST(ffmpeg_decoder_audio_only_without_tap) {
    if (!XER::Bridge::FFmpeg::IsAvailable()) {
        return;
    }
    const auto path = (TempDir() / "audio_only.wav").string();
    std::string err;

    XER::Bridge::FFmpeg::EncodeAudioParams params;
    params.output_path = path;
    params.codec_name = "pcm_s16le";
    params.sample_rate = kSampleRate;
    params.channels = kChannels;
    params.sample_format = "s16";
    const auto frames = MakeSineFrames(kSampleRate);
    if (!XER::Bridge::FFmpeg::EncodeAudioFrames(params, frames, &err)) {
        return;  // fixture unavailable — skip.
    }

    // No EnableAudioTap: opens fine, HasAudio true, but ReadAudioFrames
    // has no codec to decode with and must report failure.
    XER::Bridge::FFmpeg::VideoDecoder decoder;
    XER_UI_EXPECT_TRUE(decoder.Open(path, &err));
    if (!decoder.IsOpen()) return;
    XER_UI_EXPECT_TRUE(decoder.HasAudio());
    XER_UI_EXPECT_EQ(decoder.GetWidth(), 0);

    std::vector<std::uint8_t> bgra;
    int w = 0;
    int h = 0;
    XER_UI_EXPECT_FALSE(decoder.NextFrame(bgra, w, h));

    std::vector<XER::Bridge::FFmpeg::AudioFrameInfo> out;
    XER_UI_EXPECT_FALSE(decoder.ReadAudioFrames(&out, 8));
}

XER_UI_TEST(ffmpeg_decoder_audio_only_m4a) {
    if (!XER::Bridge::FFmpeg::IsAvailable()) {
        return;
    }
    const auto path = (TempDir() / "audio_only.m4a").string();
    std::string err;

    // Compressed-audio path (AAC in MP4/IPOD muxer) — the realistic
    // audio-track-clip case. Skips when the aac encoder is absent.
    XER::Bridge::FFmpeg::EncodeAudioParams params;
    params.output_path = path;
    params.codec_name = "aac";
    params.sample_rate = kSampleRate;
    params.channels = kChannels;
    params.sample_format = "s16";
    params.bit_rate = 128000;
    const auto frames = MakeSineFrames(kSampleRate * 2);
    if (!XER::Bridge::FFmpeg::EncodeAudioFrames(params, frames, &err)) {
        return;  // no aac encoder — skip, don't fail.
    }

    XER::Bridge::FFmpeg::VideoDecoder decoder;
    decoder.EnableAudioTap();
    XER_UI_EXPECT_TRUE(decoder.Open(path, &err));
    if (!decoder.IsOpen()) return;
    XER_UI_EXPECT_TRUE(decoder.HasAudio());
    XER_UI_EXPECT_EQ(decoder.GetWidth(), 0);
    XER_UI_EXPECT_EQ(decoder.GetHeight(), 0);

    const long long total = DrainAudio(&decoder);
    // AAC priming/padding makes the exact count fuzzy — require a
    // healthy fraction of the 96000 input samples.
    XER_UI_EXPECT_TRUE(total > 60000);
    XER_UI_EXPECT_TRUE(decoder.AudioDrained());
}

XER_UI_TEST(ffmpeg_decoder_rejects_non_media) {
    if (!XER::Bridge::FFmpeg::IsAvailable()) {
        return;
    }
    const auto path = (TempDir() / "not_media.txt").string();
    {
        std::ofstream out(path, std::ios::binary);
        out << "this is not a media file";
    }
    std::string err;
    XER::Bridge::FFmpeg::VideoDecoder decoder;
    XER_UI_EXPECT_FALSE(decoder.Open(path, &err));
    XER_UI_EXPECT_FALSE(decoder.IsOpen());
}
```

- [ ] **Step 2: Register the test file**

In `tests/CMakeLists.txt`, add `ui/ffmpeg_audio_tap_tests.cc` to the
`XER_UI_Tests` source list (after `ui/ffmpeg_video_decode_tests.cc`).
No link changes needed — `XER_UI_Core` PUBLIC-links `XER_FFmpeg`
(`src/ui/CMakeLists.txt:92-93`), which is how the existing ffmpeg test
resolves bridge symbols.

- [ ] **Step 3: Build and run — verify the new tests FAIL**

Run: `cmake --build out/build/default --target XER_UI_Tests`
Then: `out/build/default/bin/XER_UI_Tests.exe --filter audio_only`

Expected: `ffmpeg_decoder_audio_only_wav` and
`ffmpeg_decoder_audio_only_m4a` FAIL — `decoder.Open` returns false
with `err == "no video stream"` (ffmpeg_video_decoder.cc:239-243).
`ffmpeg_decoder_audio_only_without_tap` also fails at `Open`.
`ffmpeg_decoder_rejects_non_media` PASSES already (avformat rejects the
file before stream checks).

- [ ] **Step 4: Implement the decoder relaxation**

In `src/wrapper/ffmpeg/ffmpeg_video_decoder.cc`:

**4a.** Add an `Impl` method `PrepareAudioStream()` (inside the
`#ifdef _WIN32` block of `struct Impl`, near `PushAudioPacket`) that
holds the existing audio-setup block verbatim (currently lines
268-295): set `audio_stream`, `audio_start_seconds`, and when
`audio_tap` is set find/open `audio_codec`. Guard with
`if (!has_audio || audio_stream != nullptr) return;`.

**4b.** Restructure `VideoDecoder::Open` (lines 239-303): hoist the
audio-stream probe above the video check and branch:

```cpp
    p.video_stream_index = av_find_best_stream(p.fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    p.audio_stream_index =
        av_find_best_stream(p.fmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    p.has_audio = p.audio_stream_index >= 0;
    p.duration = (p.fmt->duration != AV_NOPTS_VALUE)
        ? p.fmt->duration / AV_TIME_BASE : 0.0;

    if (p.video_stream_index < 0) {
        // Audio-only container: succeed so the audio tap works.
        // codec/sws stay null, width/height stay 0, fps keeps its
        // 30.0 default; NextFrame() pumps the demuxer but returns false.
        if (!p.has_audio) {
            if (error_out) *error_out = "no decodable audio/video streams";
            return false;
        }
        p.PrepareAudioStream();
        return true;
    }

    // ... existing video codec setup unchanged (lines 244-264 minus the
    // audio block and minus the now-hoisted duration computation) ...
    p.PrepareAudioStream();   // replaces the inline audio block
    p.sws = sws_getContext(...);  // unchanged
```

**4c.** `IsOpen()` (line ~318): an audio-only decoder has
`codec == nullptr`, so key on opened objects:

```cpp
    return impl_->fmt != nullptr &&
           (impl_->codec != nullptr || impl_->audio_stream != nullptr);
```

(`audio_stream` is only set by `PrepareAudioStream()` on success paths,
so a failed `Open` still reports closed — including the A/V case where
video codec open fails before `PrepareAudioStream()` runs.)

**4d.** `NextFrame` (after the `IsOpen()` check, before the existing
send/receive loop):

```cpp
    if (p.codec == nullptr) {
        // Audio-only container: no frames exist, but the demuxer must
        // keep being pumped or the audio tap starves. Each call moves a
        // bounded batch of packets into the tap queue, then reports
        // end-of-stream. Consumers loop NextFrame()/ReadAudioFrames()
        // until AudioDrained().
        constexpr int kPumpBudget = 256;
        for (int i = 0; i < kPumpBudget; ++i) {
            const int read_ret = av_read_frame(p.fmt, p.packet);
            if (read_ret == AVERROR_EOF) {
                av_packet_unref(p.packet);
                p.audio_demux_eof = true;
                break;
            }
            if (read_ret < 0) {
                break;
            }
            if (p.audio_tap &&
                p.packet->stream_index == p.audio_stream_index &&
                p.audio_codec != nullptr) {
                p.PushAudioPacket(p.packet);
            }
            av_packet_unref(p.packet);
        }
        return false;
    }
```

**4e.** `Seek` (lines 447-478): guard the null video codec and pick a
valid fallback stream index:

```cpp
    if (ret < 0) {
        const int fallback_stream =
            p.video_stream_index >= 0 ? p.video_stream_index
                                      : p.audio_stream_index;
        if (fallback_stream >= 0) {
            ret = av_seek_frame(p.fmt, fallback_stream, ts,
                                AVSEEK_FLAG_BACKWARD);
        }
    }
    if (ret < 0) {
        return false;
    }
    if (p.codec != nullptr) {
        avcodec_flush_buffers(p.codec);
    }
    // ... FlushAudioState() / position_seconds / return true unchanged
```

**4f.** Update the class doc comment in
`src/wrapper/ffmpeg/ffmpeg_video_decoder.h`: document that audio-only
containers open successfully (`HasAudio()==true`, `GetWidth/Height()==0`),
`NextFrame()` always returns false but must still be called to pump the
demuxer, and audio-only decode is driven by alternating
`NextFrame()`/`ReadAudioFrames()` until `AudioDrained()`.

- [ ] **Step 5: Build and run — verify tests PASS**

Run: `cmake --build out/build/default --target XER_UI_Tests`
Then: `out/build/default/bin/XER_UI_Tests.exe --filter audio`

Expected: all four new tests PASS (`audio_only_wav`,
`audio_only_without_tap`, `audio_only_m4a`, `rejects_non_media`), plus
`ffmpeg_video_decoder_decodes_full_duration` still passes (no
regression on A/V files).

- [ ] **Step 6: Commit**

```bash
git add src/wrapper/ffmpeg/ffmpeg_video_decoder.cc \
        src/wrapper/ffmpeg/ffmpeg_video_decoder.h \
        tests/ui/ffmpeg_audio_tap_tests.cc tests/CMakeLists.txt
git commit -m "Accept audio-only containers in FFmpeg VideoDecoder"
```

---

## Task 3: Extract shared A/V encoder internals

**Files:**
- Modify: `src/wrapper/ffmpeg/ffmpeg_engine_bridge.cc` only
  (helpers go in the existing anonymous namespace, inside the
  `#if ENGINE_HAS_FFMPEG_BRIDGE` region that ends ~line 806; no header
  change — everything stays internal).

**Interfaces:**
- Consumes: existing helpers `SetError`, `ErrorString`,
  `PushEncoderPackets`, `SelectEncoderSampleRate`,
  `SelectEncoderSampleFormat`, `SelectEncoderChannels`,
  `SetDefaultChannelLayout`, `ResolveSampleFormat`,
  `CopyAudioFrameToAvFrame`, `SupportedPixelFormats`.
- Produces (anonymous-namespace, exact signatures):

```cpp
AVPixelFormat ParseInputPixelFormat(std::string text);

struct VideoEncodeState {
    AVStream* stream = nullptr;
    AVCodecContext* codec_context = nullptr;
    SwsContext* sws_context = nullptr;   // input -> encoder pix fmt
    AVPixelFormat input_pix_fmt = AV_PIX_FMT_NONE;
    int width = 0;
    int height = 0;
    int64_t next_pts = 0;
};
struct AudioEncodeState {
    AVStream* stream = nullptr;
    AVCodecContext* codec_context = nullptr;
    SwrContext* swr_context = nullptr;   // input -> encoder sample fmt
    AVAudioFifo* fifo = nullptr;
    AVSampleFormat input_sample_format = AV_SAMPLE_FMT_NONE;
    int input_sample_rate = 0;
    int input_channels = 0;
    int64_t next_pts = 0;
    int preferred_frame_size = 0;
};

void ReleaseVideoEncodeState(VideoEncodeState* state);
void ReleaseAudioEncodeState(AudioEncodeState* state);

bool OpenOutputAndWriteHeader(AVFormatContext* output_context,
                              const std::string& output_path,
                              std::string* out_error);
bool SetupVideoEncoder(AVFormatContext* output_context,
                       const EncodeVideoParams& params,
                       int width, int height,
                       const VideoFrameInfo& first_frame,
                       VideoEncodeState* state,
                       std::string* out_error);
bool FeedVideoEncoderFrame(AVFormatContext* output_context,
                           VideoEncodeState* state,
                           const VideoFrameInfo& input,
                           AVPacket* packet,
                           std::string* out_error);
bool SetupAudioEncoder(AVFormatContext* output_context,
                       const EncodeAudioParams& params,
                       const AudioFrameInfo& first_frame,
                       AudioEncodeState* state,
                       std::string* out_error);
bool FeedAudioEncoderFrame(AVFormatContext* output_context,
                           AudioEncodeState* state,
                           const AudioFrameInfo& input,
                           AVPacket* packet,
                           std::string* out_error);
bool FlushAudioEncoderFifo(AVFormatContext* output_context,
                           AudioEncodeState* state,
                           AVPacket* packet,
                           std::string* out_error);
bool FlushEncoderCodec(AVFormatContext* output_context,
                       AVCodecContext* codec_context,
                       AVStream* stream,
                       AVPacket* packet,
                       std::string* out_error);
```

This is a **pure refactor** — no behavior change. All bodies are moved
verbatim from the existing functions; Task 4 composes them. Existing
tests are the safety net: `ffmpeg_video_decode_tests` exercises
`EncodeVideoFrameStream`; Task 2's wav test hard-asserts
`EncodeAudioFrames` (fixture generation), so a broken refactor fails
loudly rather than skipping.

- [ ] **Step 1: Add video-side helpers + rewrite `EncodeVideoFrameStream`**

Move code **verbatim** into the anonymous namespace:

- `ParseInputPixelFormat`: the `parse_input_pix_fmt` lambda body
  (lines 1527-1535) as a free function.
- `SetupVideoEncoder`: lines 1544-1604 (codec lookup →
  `avformat_new_stream` → `codec_context` config incl.
  time_base/framerate/bit_rate/pix_fmt/`AVFMT_GLOBALHEADER` →
  `avcodec_open2` → `avcodec_parameters_from_context` →
  `stream->time_base`) **plus** lines 1639-1675 (input pixel-format
  parse + `sws_getContext` when formats differ). Everything is stored
  into `state`; the function does NOT touch `avio_open` /
  `avformat_write_header` (those stay container-level in callers).
  Note: validating the input pixel format inside Setup makes it fail
  *before* the header is written — strictly better than today (which
  writes a header then fails); call this out in the commit message.
- `FeedVideoEncoderFrame`: the per-frame body of the loop at lines
  1688-1771 (dimension/`line_sizes`/`data` validation → `av_frame_alloc`
  → `av_image_fill_arrays` → `sws_scale`/`av_image_copy` →
  `avcodec_send_frame` → `PushEncoderPackets`), reading/writing
  `state->next_pts`, `state->width/height`, `state->input_pix_fmt`,
  `state->sws_context`.
- `FlushEncoderCodec`: `avcodec_send_frame(ctx, nullptr)` +
  `PushEncoderPackets` (the shared drain tail used by both encoders).
- `OpenOutputAndWriteHeader`: the `avio_open` (when
  `!(oformat->flags & AVFMT_NOFILE)`) + `avformat_write_header` block
  (lines 1606-1625 pattern), closing `pb` on failure.
- `ReleaseVideoEncodeState`: `avcodec_free_context(&codec_context)` +
  `sws_freeContext(sws_context)` + null the pointers.

Then rewrite `EncodeVideoFrameStream` (lines 1490-1812) to:
validate (unchanged) → `provider(0)` → resolve/validate width/height
(unchanged lines 1515-1525) → `avformat_alloc_output_context2` →
`SetupVideoEncoder` → `OpenOutputAndWriteHeader` → `av_packet_alloc` →
loop `i in [0, frame_count)`: feed `first_frame` for `i==0` else
`provider(i)` → `FeedVideoEncoderFrame` → `FlushEncoderCodec` →
`av_write_trailer` → cleanup (`ReleaseVideoEncodeState`,
`av_packet_free`, `avio_closep` when `pb` open, `avformat_free_context`).
Every exit path after allocation must run the cleanup for whatever was
allocated — mirror the existing manual-cleanup blocks; a `success` flag
+ single epilogue is preferred over duplicating them.

- [ ] **Step 2: Build + run video encode regression**

Run: `cmake --build out/build/default --target XER_UI_Tests`
Then: `out/build/default/bin/XER_UI_Tests.exe --filter ffmpeg_video`

Expected: `ffmpeg_video_decoder_decodes_full_duration` PASSES (it
encodes via `EncodeVideoFrameStream` then decodes — covers the refactor).

- [ ] **Step 3: Add audio-side helpers + rewrite `EncodeAudioFrames`**

Move code **verbatim** into the anonymous namespace:

- `SetupAudioEncoder`: lines 1827-1895 minus the `frames`-vector
  dependency — sample rate/channels come from `params` with fallback to
  `first_frame` (same formula: `params.X > 0 ? params.X :
  first_frame.X`), then codec lookup (`params.codec_name` or
  `oformat->audio_codec`) → `avformat_new_stream` →
  `codec_context` (`SelectEncoderSampleRate`,
  `SelectEncoderSampleFormat`, bit_rate, `time_base {1, rate}`,
  `SelectEncoderChannels` + `SetDefaultChannelLayout`,
  `AVFMT_GLOBALHEADER`) → `avcodec_open2` →
  `avcodec_parameters_from_context` → `stream->time_base` →
  `ResolveSampleFormat(first_frame.sample_format, first_frame.planar)`
  → `swr_alloc_set_opts2` + `swr_init` → `av_audio_fifo_alloc`. Store
  `input_sample_rate`, `input_channels`, `input_sample_format`,
  `preferred_frame_size` (`codec_context->frame_size > 0 ? frame_size
  : 1152`) in state.
- `FeedAudioEncoderFrame`: per-frame body of the loop at lines
  1994-2090 (`CopyAudioFrameToAvFrame` → `swr_convert` →
  `av_audio_fifo_realloc`/`write` → drain all full
  `preferred_audio_frame_size` frames via send+`PushEncoderPackets`),
  **plus** a consistency check the batch version got for free:
  `input.sample_rate == state->input_sample_rate &&
  input.channels == state->input_channels &&
  ResolveSampleFormat(input.sample_format, input.planar) ==
  state->input_sample_format && input.sample_count > 0` — else
  `SetError("EncodeAudioFrames requires a consistent audio stream")`
  (same message the batch check uses).
- `FlushAudioEncoderFifo`: the remainder-with-silence-padding loop at
  lines 2092-2155 verbatim.
- `ReleaseAudioEncodeState`: `avcodec_free_context` + `swr_free` +
  `av_audio_fifo_free` + null pointers.

Then rewrite `EncodeAudioFrames` (lines 1814-2186) to: keep the
`frames.empty()`/`output_path`/rate-channels/`consistent_stream`
validation **verbatim** → `avformat_alloc_output_context2` →
`SetupAudioEncoder(ctx, params, frames.front(), &astate)` →
`OpenOutputAndWriteHeader` → `av_packet_alloc` → loop frames →
`FeedAudioEncoderFrame` → `FlushAudioEncoderFifo` → `FlushEncoderCodec`
→ `av_write_trailer` → same cleanup discipline.

- [ ] **Step 4: Build + run audio encode regression**

Run: `cmake --build out/build/default --target XER_UI_Tests`
Then: `out/build/default/bin/XER_UI_Tests.exe --filter audio_only_wav`

Expected: `ffmpeg_decoder_audio_only_wav` PASSES — its fixture is
encoded by `EncodeAudioFrames`, so this verifies the refactor end to
end. Also re-run `--filter ffmpeg` for the whole bridge suite.

- [ ] **Step 5: Commit**

```bash
git add src/wrapper/ffmpeg/ffmpeg_engine_bridge.cc
git commit -m "Extract shared A/V encoder internals for the upcoming mux API"
```

---

## Task 4: `EncodeAudioVideoStream` mux API

**Files:**
- Modify: `src/wrapper/ffmpeg/ffmpeg_engine_bridge.h` (declarations)
- Modify: `src/wrapper/ffmpeg/ffmpeg_engine_bridge.cc` (implementation,
  after `EncodeAudioFrames` ~line 2186)
- Create: `tests/ui/ffmpeg_av_mux_tests.cc`
- Modify: `tests/CMakeLists.txt` (add the new file to `XER_UI_Tests`)

**Interfaces:**
- Consumes: Task 3's `VideoEncodeState`/`AudioEncodeState` helpers;
  `VideoFrameProvider`, `VideoFrameInfo`, `AudioFrameInfo`,
  `EncodeVideoParams`, `EncodeAudioParams` (all pre-existing).
- Produces (exact spec signatures, in `XER::Bridge::FFmpeg`):

```cpp
struct EncodeAudioVideoParams {
    EncodeVideoParams video;   // output_path/codec/dimensions/fps/bitrate
    EncodeAudioParams audio;   // codec/sample rate/channels/format/bitrate
};

using AudioFrameStreamProvider =
    std::function<bool(std::size_t frame_index, AudioFrameInfo* out_frame,
                       std::string* error)>;

bool EncodeAudioVideoStream(
    const EncodeAudioVideoParams& params,
    std::size_t video_frame_count, const VideoFrameProvider& video_provider,
    std::size_t audio_frame_count,
    const AudioFrameStreamProvider& audio_provider,
    std::string* error_out);
```

Semantics (builder-resolved spec gaps — see "Spec gaps" in the report):
output path is `params.video.output_path`;
`params.audio.output_path` is ignored; `audio_frame_count == 0` writes a
video-only file (no audio stream is created);
`video_frame_count == 0` is an error; providers are called with indices
`[0, count)` exactly once each; interleave feeds whichever side's next
input frame has the earlier start time (ties → video).

- [ ] **Step 1: Add the header declarations**

In `src/wrapper/ffmpeg/ffmpeg_engine_bridge.h`, after the
`EncodeAudioFrames` declaration (~line 236):

```cpp
struct EncodeAudioVideoParams {
    EncodeVideoParams video;
    EncodeAudioParams audio;
};

// Streams interleaved video+audio into ONE container: a single
// AVFormatContext with one video stream (params.video) and, when
// audio_frame_count > 0, one audio stream (params.audio). Output path
// comes from params.video.output_path; params.audio.output_path is
// ignored. video_frame_count == 0 is an error; audio_frame_count == 0
// produces a video-only file. Providers are invoked with frame_index in
// [0, count) exactly once; a false return aborts with error_out set.
using AudioFrameStreamProvider =
    std::function<bool(std::size_t frame_index, AudioFrameInfo* out_frame,
                       std::string* error)>;

bool EncodeAudioVideoStream(
    const EncodeAudioVideoParams& params,
    std::size_t video_frame_count, const VideoFrameProvider& video_provider,
    std::size_t audio_frame_count,
    const AudioFrameStreamProvider& audio_provider,
    std::string* error_out);
```

- [ ] **Step 2: Write the failing tests**

Create `tests/ui/ffmpeg_av_mux_tests.cc`:

```cpp
// Round-trip tests for EncodeAudioVideoStream: synthetic BGRA frames +
// synthetic PCM chunks -> .mp4 with two streams -> ProbeMedia shows
// video+audio -> DecodeVideoFrames/DecodeAudioFrames sanity. Also pins
// the edge cases: audio_frame_count==0 yields video-only output, and
// video_frame_count==0 / mid-stream provider failure are errors.

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "test_harness.h"
#include "wrapper/ffmpeg/ffmpeg_engine_bridge.h"

namespace {

constexpr int kW = 320;
constexpr int kH = 240;
constexpr int kFrames = 90;        // 3 s @ 30 fps
constexpr int kFps = 30;
constexpr int kRate = 48000;
constexpr int kAudioChunks = 141;  // ceil(3 s * 48000 / 1024) = 140.6

std::filesystem::path TempDir() {
    const auto dir = std::filesystem::temp_directory_path() /
                     "xer_ui_tests_mux";
    std::filesystem::create_directories(dir);
    return dir;
}

// Solid BGRA frame with a moving vertical bar (same pattern as
// ffmpeg_video_decode_tests.cc — enough motion for real frames).
void FillVideoFrame(XER::Bridge::FFmpeg::VideoFrameInfo* frame,
                    std::size_t index) {
    frame->width = kW;
    frame->height = kH;
    frame->pixel_format = "bgra";
    frame->line_sizes = {kW * 4, 0, 0, 0};
    frame->data.assign(static_cast<std::size_t>(kW) * kH * 4u, 0);
    const int bar_x = static_cast<int>(index * 5) % kW;
    for (int y = 0; y < kH; ++y) {
        for (int x = 0; x < kW; ++x) {
            const bool bar = x >= bar_x && x < bar_x + 24;
            const std::size_t o =
                (static_cast<std::size_t>(y) * kW + x) * 4u;
            frame->data[o + 0] = bar ? 40 : 30;    // B
            frame->data[o + 1] = bar ? 120 : 60;   // G
            frame->data[o + 2] = bar ? 220 : 90;   // R
            frame->data[o + 3] = 255;              // A
        }
    }
}

bool VideoProvider(std::size_t index,
                   XER::Bridge::FFmpeg::VideoFrameInfo* out,
                   std::string*) {
    FillVideoFrame(out, index);
    return true;
}

// 440 Hz stereo s16 sine; chunk `index` covers samples
// [index*1024, min(index*1024+1024, 3 s)) — last chunk is short (640).
void FillAudioChunk(std::size_t first_sample, int sample_count,
                    XER::Bridge::FFmpeg::AudioFrameInfo* out) {
    out->sample_rate = kRate;
    out->channels = 2;
    out->sample_count = sample_count;
    out->planar = false;
    out->sample_format = "s16";
    out->data.assign(
        static_cast<std::size_t>(sample_count) * 2 * 2u, 0);
    auto* pcm = reinterpret_cast<std::int16_t*>(out->data.data());
    for (int i = 0; i < sample_count; ++i) {
        const double t =
            static_cast<double>(first_sample + i) / kRate;
        const std::int16_t v = static_cast<std::int16_t>(
            12000.0 * std::sin(2.0 * 3.14159265358979323846 * 440.0 * t));
        pcm[i * 2 + 0] = v;
        pcm[i * 2 + 1] = v;
    }
}

bool AudioProvider(std::size_t index,
                   XER::Bridge::FFmpeg::AudioFrameInfo* out,
                   std::string*) {
    const int start = static_cast<int>(index) * 1024;
    const int total = kRate * 3;  // 3 s, matches the video duration
    if (start >= total) return false;
    FillAudioChunk(static_cast<std::size_t>(start),
                   std::min(1024, total - start), out);
    return true;
}

XER::Bridge::FFmpeg::EncodeAudioVideoParams MakeParams(
    const std::string& path) {
    XER::Bridge::FFmpeg::EncodeAudioVideoParams params;
    params.video.output_path = path;
    params.video.width = kW;
    params.video.height = kH;
    params.video.fps_num = kFps;
    params.video.fps_den = 1;
    params.video.pixel_format = "bgra";
    params.video.bit_rate = 800000;
    params.audio.codec_name = "aac";
    params.audio.sample_rate = kRate;
    params.audio.channels = 2;
    params.audio.sample_format = "s16";
    params.audio.bit_rate = 128000;
    return params;
}

// libx264 first (real B-frames), mpeg4 fallback — same pattern as the
// decode test. AAC is a native encoder, always present.
bool EncodeWithFallback(
    XER::Bridge::FFmpeg::EncodeAudioVideoParams* params,
    std::size_t video_count, std::size_t audio_count,
    std::string* err) {
    for (const char* codec : {"libx264", "mpeg4"}) {
        params->video.codec_name = codec;
        err->clear();
        if (XER::Bridge::FFmpeg::EncodeAudioVideoStream(
                *params, video_count, VideoProvider, audio_count,
                AudioProvider, err)) {
            return true;
        }
    }
    return false;
}

}  // namespace

XER_UI_TEST(ffmpeg_encode_audio_video_stream_muxes_mp4) {
    if (!XER::Bridge::FFmpeg::IsAvailable()) {
        return;
    }
    const auto path = (TempDir() / "mux_av.mp4").string();
    std::string err;
    auto params = MakeParams(path);
    if (!EncodeWithFallback(&params, kFrames, kAudioChunks, &err)) {
        std::cerr << "mux encode failed: " << err << '\n';
        XER_UI_EXPECT_TRUE(false);
        return;
    }

    XER::Bridge::FFmpeg::MediaInfo info;
    XER_UI_EXPECT_TRUE(XER::Bridge::FFmpeg::ProbeMedia(path, &info, &err));
    XER_UI_EXPECT_EQ(info.stream_count, 2);
    int video_index = -1;
    int audio_index = -1;
    for (const auto& s : info.streams) {
        if (s.media_type == "video") video_index = s.index;
        if (s.media_type == "audio") audio_index = s.index;
    }
    XER_UI_EXPECT_TRUE(video_index >= 0);
    XER_UI_EXPECT_TRUE(audio_index >= 0);

    std::vector<XER::Bridge::FFmpeg::VideoFrameInfo> vframes;
    XER_UI_EXPECT_TRUE(XER::Bridge::FFmpeg::DecodeVideoFrames(
        path, video_index, 10, &vframes, &err));
    XER_UI_EXPECT_TRUE(vframes.size() >= 5);
    if (!vframes.empty()) {
        XER_UI_EXPECT_EQ(vframes.front().width, kW);
        XER_UI_EXPECT_EQ(vframes.front().height, kH);
    }

    std::vector<XER::Bridge::FFmpeg::AudioFrameInfo> aframes;
    XER_UI_EXPECT_TRUE(XER::Bridge::FFmpeg::DecodeAudioFrames(
        path, audio_index, 10, &aframes, &err));
    XER_UI_EXPECT_TRUE(!aframes.empty());
    if (!aframes.empty()) {
        XER_UI_EXPECT_TRUE(aframes.front().sample_count > 0);
    }
}

XER_UI_TEST(ffmpeg_encode_audio_video_stream_video_only) {
    if (!XER::Bridge::FFmpeg::IsAvailable()) {
        return;
    }
    const auto path = (TempDir() / "mux_video_only.mp4").string();
    std::string err;
    auto params = MakeParams(path);
    // audio_frame_count == 0 -> video-only file, no audio stream.
    if (!EncodeWithFallback(&params, kFrames, 0, &err)) {
        std::cerr << "video-only mux encode failed: " << err << '\n';
        XER_UI_EXPECT_TRUE(false);
        return;
    }
    XER::Bridge::FFmpeg::MediaInfo info;
    XER_UI_EXPECT_TRUE(XER::Bridge::FFmpeg::ProbeMedia(path, &info, &err));
    XER_UI_EXPECT_EQ(info.stream_count, 1);
    XER_UI_EXPECT_EQ(info.streams.front().media_type, "video");
}

XER_UI_TEST(ffmpeg_encode_audio_video_stream_rejects_empty_video) {
    if (!XER::Bridge::FFmpeg::IsAvailable()) {
        return;
    }
    const auto path = (TempDir() / "mux_empty.mp4").string();
    std::string err;
    auto params = MakeParams(path);
    XER_UI_EXPECT_FALSE(XER::Bridge::FFmpeg::EncodeAudioVideoStream(
        params, 0, VideoProvider, kAudioChunks, AudioProvider, &err));
    XER_UI_EXPECT_TRUE(!err.empty());
}

XER_UI_TEST(ffmpeg_encode_audio_video_stream_provider_error) {
    if (!XER::Bridge::FFmpeg::IsAvailable()) {
        return;
    }
    const auto path = (TempDir() / "mux_fail.mp4").string();
    std::string err;
    auto params = MakeParams(path);
    params.video.codec_name = "libx264";
    // Video provider dies at frame 5 — mux must fail, not truncate
    // silently.
    auto bad_video = [](std::size_t index,
                        XER::Bridge::FFmpeg::VideoFrameInfo* out,
                        std::string* perr) {
        if (index >= 5) {
            *perr = "injected provider failure";
            return false;
        }
        FillVideoFrame(out, index);
        return true;
    };
    XER_UI_EXPECT_FALSE(XER::Bridge::FFmpeg::EncodeAudioVideoStream(
        params, kFrames, bad_video, kAudioChunks, AudioProvider, &err));
}
```

Register `ui/ffmpeg_av_mux_tests.cc` in the `XER_UI_Tests` source list
in `tests/CMakeLists.txt`.

- [ ] **Step 3: Build — verify tests FAIL to link/run**

Run: `cmake --build out/build/default --target XER_UI_Tests`
Expected: **link error** — `EncodeAudioVideoStream` and
`EncodeAudioVideoParams` are declared but not defined (header exists,
no implementation yet). This is the TDD "red" for a new API.

- [ ] **Step 4: Implement `EncodeAudioVideoStream`**

In `ffmpeg_engine_bridge.cc`, after `EncodeAudioFrames` (~line 2186),
inside `#if ENGINE_HAS_FFMPEG_BRIDGE` … `#else` … `#endif`:

```cpp
bool EncodeAudioVideoStream(const EncodeAudioVideoParams& params,
                            std::size_t video_frame_count,
                            const VideoFrameProvider& video_provider,
                            std::size_t audio_frame_count,
                            const AudioFrameStreamProvider& audio_provider,
                            std::string* out_error) {
    // 1. Validate: video_frame_count > 0; providers non-null;
    //    params.video.output_path non-empty; audio provider required
    //    iff audio_frame_count > 0.
    // 2. Probe frame 0 of each side: video_provider(0, &v0), and
    //    audio_provider(0, &a0) when has_audio. Resolve/validate video
    //    width/height exactly like EncodeVideoFrameStream
    //    (params override, else first_frame; first_frame dims must
    //    match and line_sizes/data must be non-empty).
    // 3. avformat_alloc_output_context2 on params.video.output_path.
    // 4. SetupVideoEncoder(ctx, params.video, w, h, v0, &vstate, err);
    //    when has_audio: SetupAudioEncoder(ctx, params.audio, a0,
    //    &astate, err). audio_frame_count == 0 -> skip audio entirely.
    // 5. OpenOutputAndWriteHeader(ctx, params.video.output_path, err).
    // 6. av_packet_alloc (one packet, feeds are sequential).
    // 7. Interleave loop:
    //      video_frame_seconds = fps_den/fps_num (params.video; fall
    //          back 1/25 when unset)
    //      video_next_t = audio_next_t = 0.0; video_done = audio_done = 0
    //      while (video_done < video_frame_count ||
    //             audio_done < audio_frame_count):
    //        take_video = video_done < video_frame_count &&
    //            (audio_done >= audio_frame_count ||
    //             video_next_t <= audio_next_t);
    //        video branch: input = (video_done==0) ? v0 :
    //            video_provider(video_done, &in, err) — provider false
    //            -> interleave_ok=false, break;
    //            FeedVideoEncoderFrame(ctx,&vstate,input,packet,err);
    //            video_next_t += video_frame_seconds; ++video_done;
    //        audio branch: same shape with a0 / audio_provider /
    //            FeedAudioEncoderFrame; then
    //            audio_next_t += input.sample_count /
    //                            double(input.sample_rate)  (guard >0);
    //            ++audio_done;
    // 8. On interleave_ok: FlushEncoderCodec(video) then, when
    //    has_audio, FlushAudioEncoderFifo(audio) +
    //    FlushEncoderCodec(audio). On any failure skip remaining steps.
    // 9. Trailer: on success check av_write_trailer result; on failure
    //    still call av_write_trailer best-effort (matches existing
    //    encode functions).
    // 10. Cleanup on EVERY exit path after step 3: Release*State for
    //     whichever Setup succeeded, av_packet_free, avio_closep when
    //     pb open, avformat_free_context. Trailer only once the header
    //     was written (i.e., past step 5).
    // #else branch: SetError("FFmpeg bridge unavailable"); return false.
}
```

Key correctness notes for the implementer:

- `av_interleaved_write_frame` (inside `PushEncoderPackets`) already
  buffers and dts-orders packets across streams — the PTS-due feeding
  order above only needs to keep both encoders producing incrementally;
  do NOT try to write packets yourself.
- Both encoders must be flushed (`FlushEncoderCodec` /
  `FlushAudioEncoderFifo` + `FlushEncoderCodec`) BEFORE
  `av_write_trailer` or the file loses its tail / gets wrong duration.
- AAC priming (~1024 samples) may make the first audio pts non-zero —
  acceptable for v1; do not hand-shift PTS (that's 1C's
  `GetAudioStartSeconds` concern on the decode side).

- [ ] **Step 5: Build and run — verify tests PASS**

Run: `cmake --build out/build/default --target XER_UI_Tests`
Then: `out/build/default/bin/XER_UI_Tests.exe --filter mux`

Expected: all four mux tests PASS. Sanity-check the artifact once:
`ffprobe`/`ProbeMedia` already asserts 2 streams; optionally open
`%TEMP%/xer_ui_tests_mux/mux_av.mp4` in a player.

- [ ] **Step 6: Commit**

```bash
git add src/wrapper/ffmpeg/ffmpeg_engine_bridge.h \
        src/wrapper/ffmpeg/ffmpeg_engine_bridge.cc \
        tests/ui/ffmpeg_av_mux_tests.cc tests/CMakeLists.txt
git commit -m "Add EncodeAudioVideoStream two-stream A/V mux to FFmpeg bridge"
```

---

## Task 5: Full verification & wrap-up

**Files:** none new — verification only.

- [ ] **Step 1: Rebuild everything affected**

Run: `cmake --build out/build/default --target XER_UI_Tests XER_Video_Smoke XER_FFmpeg`
Expected: zero warnings-as-errors, clean build.

- [ ] **Step 2: Run the full test suite**

Run: `ctest --test-dir out/build/default --output-on-failure`
Expected: `XER_UI_Tests` and `XER_Video_Smoke` (plus pre-existing tests)
all pass. Also spot-run:
`out/build/default/bin/XER_UI_Tests.exe` (no filter) and
`out/build/default/bin/XER_Video_Smoke.exe`.

- [ ] **Step 3: Final review pass**

- `git status` clean except intended files; `git log` shows the four
  task commits.
- Confirm no FFmpeg headers leaked outside `src/wrapper/ffmpeg`
  (`grep -rn "libav\|libsw\|libyuv" tests/ tools/` should only hit
  comments).
- Confirm `XER_Video` now has a real consumer (`XER_Video_Smoke`) — the
  orphan-lib milestone for the whole Phase-1 program.

- [ ] **Step 4: Commit any stragglers** (docs/plan files if the parent
agent wants them tracked — `docs/` is currently untracked in this repo).
