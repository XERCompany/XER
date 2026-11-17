// ClipPoster suite — poster frame extraction through the streaming
// VideoDecoder (Seek keyframe-backward + bounded forward decode) and
// FrameExporter::ToFile PNG write. The whole suite self-skips when the
// FFmpeg bridge is unavailable (ENGINE_HAS_FFMPEG_BRIDGE is MSVC-only,
// same gating as XER_Video_Smoke / ffmpeg_*_tests). Fixtures are
// synthesized in %TEMP% via EncodeVideoFrameStream / EncodeAudioFrames
// — no binary test assets — following tests/ui/ffmpeg_av_mux_tests.cc
// and tests/ui/ffmpeg_audio_tap_tests.cc.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "video_editor_test_util.h"
#include "clip_poster.h"
#include "frame.h"
#include "helper/path_utils.h"
#include "wrapper/ffmpeg/ffmpeg_engine_bridge.h"

namespace {
namespace Path = XER::Helper::Path;  // UTF-8 <-> fs::path helpers
using XER::VideoEditor::ClipPoster;

std::string Utf8Path(const std::filesystem::path& p) {
    return Path::ToUtf8(p);
}

// ---- fixtures ---------------------------------------------------------

constexpr int kVideoW = 64;
constexpr int kVideoH = 48;
constexpr int kVideoFps = 15;
constexpr int kVideoFrameCount = 30;  // 2 s @ 15 fps
constexpr double kVideoDurationSec =
    static_cast<double>(kVideoFrameCount) / kVideoFps;

constexpr int kAudioRate = 48000;
constexpr int kAudioChannels = 2;
constexpr int kAudioTotalSamples = kAudioRate;  // 1 s

// 64x48 BGRA frame with a moving vertical bar — enough motion for the
// encoder to emit real frames instead of dropping them as static
// (same pattern as ffmpeg_audio_tap_tests.cc FillTinyFrame).
void FillVideoFrame(XER::Bridge::FFmpeg::VideoFrameInfo* frame,
                    std::size_t index) {
    frame->width = kVideoW;
    frame->height = kVideoH;
    frame->pixel_format = "bgra";
    frame->line_sizes = {kVideoW * 4, 0, 0, 0};
    frame->data.assign(static_cast<std::size_t>(kVideoW) * kVideoH * 4u,
                       0);
    const int bar_x = static_cast<int>(index * 3) % kVideoW;
    for (int y = 0; y < kVideoH; ++y) {
        for (int x = 0; x < kVideoW; ++x) {
            const bool bar = x >= bar_x && x < bar_x + 8;
            const std::size_t o =
                (static_cast<std::size_t>(y) * kVideoW + x) * 4u;
            frame->data[o + 0] = bar ? 40 : 30;    // B
            frame->data[o + 1] = bar ? 120 : 60;   // G
            frame->data[o + 2] = bar ? 220 : 90;   // R
            frame->data[o + 3] = 255;              // A
        }
    }
}

// Encodes kVideoFrameCount frames -> .mp4 video-only clip. Codec
// fallback {"libx264","mpeg4"} like the other ffmpeg test fixtures;
// mpeg4 is a native encoder so failure here means real breakage.
bool MakeVideoFile(const std::string& utf8_path, std::string* err) {
    XER::Bridge::FFmpeg::EncodeVideoParams params;
    params.output_path = utf8_path;
    params.width = kVideoW;
    params.height = kVideoH;
    params.pixel_format = "bgra";
    params.fps_num = kVideoFps;
    params.fps_den = 1;
    params.bit_rate = 200000;
    for (const char* codec : {"libx264", "mpeg4"}) {
        params.codec_name = codec;
        err->clear();
        if (XER::Bridge::FFmpeg::EncodeVideoFrameStream(
                params, kVideoFrameCount,
                [](std::size_t index,
                   XER::Bridge::FFmpeg::VideoFrameInfo* out,
                   std::string*) {
                    FillVideoFrame(out, index);
                    return true;
                },
                err)) {
            return true;
        }
    }
    return false;
}

// Interleaved signed-16 PCM sine (440 Hz); `first_sample` is the
// absolute sample index so chunks stay phase-continuous — same
// pattern as ffmpeg_audio_tap_tests.cc FillSine.
void FillSine(XER::Bridge::FFmpeg::AudioFrameInfo* frame,
              std::size_t first_sample, int sample_count) {
    frame->sample_rate = kAudioRate;
    frame->channels = kAudioChannels;
    frame->sample_count = sample_count;
    frame->planar = false;
    frame->sample_format = "s16";
    frame->data.assign(
        static_cast<std::size_t>(sample_count) * kAudioChannels * 2u, 0);
    auto* pcm = reinterpret_cast<std::int16_t*>(frame->data.data());
    for (int i = 0; i < sample_count; ++i) {
        const double t =
            static_cast<double>(first_sample + i) / kAudioRate;
        const std::int16_t v = static_cast<std::int16_t>(
            12000.0 * std::sin(2.0 * 3.14159265358979323846 * 440.0 * t));
        pcm[i * kAudioChannels + 0] = v;
        pcm[i * kAudioChannels + 1] = v;
    }
}

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

// Encodes 1 s of sine -> pcm_s16le WAV (deterministic, no optional
// encoder needed — same as ffmpeg_audio_tap_tests.cc EncodeWavFile).
bool MakeAudioFile(const std::string& utf8_path, std::string* err) {
    XER::Bridge::FFmpeg::EncodeAudioParams params;
    params.output_path = utf8_path;
    params.codec_name = "pcm_s16le";
    params.sample_rate = kAudioRate;
    params.channels = kAudioChannels;
    params.sample_format = "s16";
    return XER::Bridge::FFmpeg::EncodeAudioFrames(
        params, MakeSineFrames(kAudioTotalSamples), err);
}

// PNG signature check — first four bytes must be 89 50 4E 47.
bool HasPngMagic(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        return false;
    }
    char magic[4] = {};
    in.read(magic, 4);
    return in.gcount() == 4 && magic[0] == '\x89' && magic[1] == 'P' &&
           magic[2] == 'N' && magic[3] == 'G';
}

void CheckPngFile(const std::filesystem::path& path) {
    XER_VE_CHECK(std::filesystem::exists(path));
    if (!std::filesystem::exists(path)) {
        return;
    }
    XER_VE_CHECK(std::filesystem::file_size(path) > 100);
    XER_VE_CHECK(HasPngMagic(path));
}

}  // namespace

int XER_VideoEditor_RunPosterTests() {
    // Whole suite is FFmpeg-backed — skip cleanly when the bridge is a
    // stub (non-Windows / no avformat), same as XER_Video_Smoke.
    if (!XER::Bridge::FFmpeg::IsAvailable()) {
        return 0;
    }

    const auto dir = XER::VideoEditor::Test::TempDir() / "poster";
    std::filesystem::create_directories(dir);
    std::string err;

    const auto video_path = dir / "clip.mp4";
    const auto wav_path = dir / "audio_only.wav";

    if (!MakeVideoFile(Utf8Path(video_path), &err)) {
        // mpeg4 fallback is a native encoder — both codecs failing means
        // the FFmpeg build cannot encode video at all; skip rather than
        // fail the whole suite on an incapable fixture.
        std::fprintf(stderr,
                     "note: poster video fixture encode failed: %s\n",
                     err.c_str());
        return 0;
    }
    // WAV is pcm_s16le (always available); a failure here is real, but
    // only case 2 needs it — degrade that case to a skip-note instead
    // of failing unrelated video cases.
    const bool wav_ok = MakeAudioFile(Utf8Path(wav_path), &err);
    if (!wav_ok) {
        std::fprintf(stderr,
                     "note: poster audio fixture encode failed: %s\n",
                     err.c_str());
    }

    const std::string video_utf8 = Utf8Path(video_path);

    // -- 1. mid-clip seek -> real PNG on disk -------------------------
    {
        const auto out = dir / "poster_mid.png";
        err.clear();
        XER_VE_CHECK(ClipPoster::Extract(video_utf8,
                                         kVideoDurationSec * 0.5,
                                         Utf8Path(out), &err));
        CheckPngFile(out);
    }

    // -- 2. audio-only input -> clean false, no file ------------------
    if (wav_ok) {
        const auto out = dir / "poster_audio.png";
        err.clear();
        XER_VE_CHECK(!ClipPoster::Extract(Utf8Path(wav_path), 0.5,
                                          Utf8Path(out), &err));
        XER_VE_CHECK(!err.empty());
        XER_VE_CHECK(!std::filesystem::exists(out));
    }

    // -- 3. missing input -> false with non-empty error ---------------
    {
        const auto missing = dir / "does_not_exist.mp4";
        const auto out = dir / "poster_missing.png";
        err.clear();
        XER_VE_CHECK(!ClipPoster::Extract(Utf8Path(missing), 0.5,
                                          Utf8Path(out), &err));
        XER_VE_CHECK(!err.empty());
        XER_VE_CHECK(!std::filesystem::exists(out));
    }

    // -- 4. out-of-range seeks clamp instead of failing ---------------
    {
        const auto out_beyond = dir / "poster_beyond.png";
        err.clear();
        XER_VE_CHECK(ClipPoster::Extract(video_utf8, 999.0,
                                         Utf8Path(out_beyond), &err));
        CheckPngFile(out_beyond);

        const auto out_negative = dir / "poster_negative.png";
        err.clear();
        XER_VE_CHECK(ClipPoster::Extract(video_utf8, -5.0,
                                         Utf8Path(out_negative), &err));
        CheckPngFile(out_negative);
    }

    // -- 5. nested output path -> parent dirs created -----------------
    {
        const auto out = dir / "nested" / "deeper" / "poster.png";
        err.clear();
        XER_VE_CHECK(ClipPoster::Extract(video_utf8, 0.5,
                                         Utf8Path(out), &err));
        CheckPngFile(out);
    }

    // -- 6. GetPosterAt: arbitrary source time -> in-memory frame -----
    {
        video::Frame at0;
        err.clear();
        XER_VE_CHECK(ClipPoster::GetPosterAt(video_utf8, 0.0, &at0,
                                             &err));
        XER_VE_CHECK(at0.IsValid());
        XER_VE_CHECK(at0.Width() == kVideoW);
        XER_VE_CHECK(at0.Height() == kVideoH);
        XER_VE_CHECK(at0.Format() == video::PixelFormat::BGRA8);
        XER_VE_CHECK(at0.DataSize() ==
                     static_cast<std::size_t>(kVideoW) * kVideoH * 4u);

        // A later time must land on a different frame — the fixture's
        // bar moves every frame, so the pixels differ.
        video::Frame at_late;
        err.clear();
        XER_VE_CHECK(ClipPoster::GetPosterAt(
            video_utf8, kVideoDurationSec * 0.75, &at_late, &err));
        XER_VE_CHECK(at_late.IsValid());
        XER_VE_CHECK(at_late.DataSize() == at0.DataSize());
        if (at0.IsValid() && at_late.IsValid() &&
            at0.DataSize() == at_late.DataSize()) {
            XER_VE_CHECK(std::memcmp(at0.Data(), at_late.Data(),
                                     at0.DataSize()) != 0);
        }

        // Out-of-range times fall back to the mid-stream frame — a
        // valid frame that differs from the t=0 poster (bar moved).
        video::Frame at_negative;
        err.clear();
        XER_VE_CHECK(ClipPoster::GetPosterAt(video_utf8, -5.0,
                                             &at_negative, &err));
        XER_VE_CHECK(at_negative.IsValid());
        video::Frame at_beyond;
        err.clear();
        XER_VE_CHECK(ClipPoster::GetPosterAt(video_utf8, 999.0,
                                             &at_beyond, &err));
        XER_VE_CHECK(at_beyond.IsValid());
        if (at0.IsValid() && at_negative.IsValid() &&
            at0.DataSize() == at_negative.DataSize()) {
            XER_VE_CHECK(std::memcmp(at0.Data(), at_negative.Data(),
                                     at0.DataSize()) != 0);
        }
    }

    // -- 7. GetPosterAt failure paths ----------------------------------
    {
        video::Frame frame;
        // Null out-param -> false.
        err.clear();
        XER_VE_CHECK(!ClipPoster::GetPosterAt(video_utf8, 0.5, nullptr,
                                              &err));
        // Missing file -> false with non-empty error.
        const auto missing = dir / "does_not_exist.mp4";
        err.clear();
        XER_VE_CHECK(!ClipPoster::GetPosterAt(Utf8Path(missing), 0.5,
                                              &frame, &err));
        XER_VE_CHECK(!err.empty());
        // Audio-only container -> false, frame untouched.
        if (wav_ok) {
            err.clear();
            XER_VE_CHECK(!ClipPoster::GetPosterAt(Utf8Path(wav_path),
                                                  0.5, &frame, &err));
            XER_VE_CHECK(!err.empty());
            XER_VE_CHECK(!frame.IsValid());
        }
    }

    return 0;
}
