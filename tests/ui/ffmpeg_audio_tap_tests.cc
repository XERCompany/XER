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

// Encodes `sample_count` samples of the shared sine fixture to a WAV
// (pcm_s16le — deterministic, no optional encoder needed).
bool EncodeWavFile(const std::string& path, int sample_count,
                    std::string* err) {
    XER::Bridge::FFmpeg::EncodeAudioParams params;
    params.output_path = path;
    params.codec_name = "pcm_s16le";
    params.sample_rate = kSampleRate;
    params.channels = kChannels;
    params.sample_format = "s16";
    return XER::Bridge::FFmpeg::EncodeAudioFrames(
        params, MakeSineFrames(sample_count), err);
}

constexpr int kTinyW = 64;
constexpr int kTinyH = 64;

// One 64x64 BGRA frame with a moving vertical bar — enough motion for the
// encoder to emit real frames instead of dropping them as static.
void FillTinyFrame(XER::Bridge::FFmpeg::VideoFrameInfo* frame,
                   std::size_t index) {
    frame->width = kTinyW;
    frame->height = kTinyH;
    frame->pixel_format = "bgra";
    frame->line_sizes = {kTinyW * 4, 0, 0, 0};
    frame->data.assign(static_cast<size_t>(kTinyW) * kTinyH * 4u, 0);
    const int bar_x = static_cast<int>(index * 4) % kTinyW;
    for (int y = 0; y < kTinyH; ++y) {
        for (int x = bar_x; x < bar_x + 8 && x < kTinyW; ++x) {
            const size_t o = (static_cast<size_t>(y) * kTinyW + x) * 4u;
            frame->data[o + 2] = 220;  // R
            frame->data[o + 3] = 255;  // A
        }
    }
}

// Encodes a 12-frame 64x64@24fps video-only clip — no audio stream, for
// decoder-reuse and tap-without-audio coverage. Returns false when no
// usable video encoder exists so callers can skip.
bool EncodeTinyVideo(const std::string& path, std::string* err) {
    XER::Bridge::FFmpeg::EncodeVideoParams params;
    params.output_path = path;
    params.width = kTinyW;
    params.height = kTinyH;
    params.fps_num = 24;
    params.fps_den = 1;
    params.bit_rate = 200000;
    for (const char* codec : {"libx264", "mpeg4"}) {
        params.codec_name = codec;
        err->clear();
        if (XER::Bridge::FFmpeg::EncodeVideoFrameStream(
                params, 12,
                [](std::size_t i,
                   XER::Bridge::FFmpeg::VideoFrameInfo* out,
                   std::string* /*provider_err*/) {
                    FillTinyFrame(out, i);
                    return true;
                },
                err)) {
            return true;
        }
    }
    return false;
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

// Regression: Open() calls Close() first, so one decoder instance can be
// reused across files — but the audio-only path never assigns
// width/height/fps/position_seconds. They must be reset on Close() or a
// video→audio-only reuse keeps reporting the previous file's geometry.
XER_UI_TEST(ffmpeg_decoder_reuse_video_then_audio_only) {
    if (!XER::Bridge::FFmpeg::IsAvailable()) {
        return;
    }
    std::string err;
    const auto video_path = (TempDir() / "reuse_video.mp4").string();
    if (!EncodeTinyVideo(video_path, &err)) {
        return;  // no video encoder — skip.
    }
    const auto audio_path = (TempDir() / "reuse_audio.wav").string();
    if (!EncodeWavFile(audio_path, kSampleRate, &err)) {
        XER_UI_EXPECT_TRUE(false);
        return;
    }

    XER::Bridge::FFmpeg::VideoDecoder decoder;
    decoder.EnableAudioTap();
    XER_UI_EXPECT_TRUE(decoder.Open(video_path, &err));
    if (!decoder.IsOpen()) return;
    XER_UI_EXPECT_TRUE(decoder.GetWidth() > 0);
    XER_UI_EXPECT_TRUE(decoder.GetHeight() > 0);

    // Decode at least one frame so position_seconds leaves its -1.0
    // initial value — the reset asserted below is then observable.
    std::vector<std::uint8_t> bgra;
    int w = 0;
    int h = 0;
    bool got_frame = false;
    for (int i = 0; i < 10 && !got_frame; ++i) {
        got_frame = decoder.NextFrame(bgra, w, h);
    }
    XER_UI_EXPECT_TRUE(got_frame);
    XER_UI_EXPECT_TRUE(decoder.GetPositionSeconds() >= 0.0);

    XER_UI_EXPECT_TRUE(decoder.Open(audio_path, &err));
    if (!decoder.IsOpen()) return;
    XER_UI_EXPECT_TRUE(decoder.HasAudio());
    XER_UI_EXPECT_EQ(decoder.GetWidth(), 0);
    XER_UI_EXPECT_EQ(decoder.GetHeight(), 0);
    XER_UI_EXPECT_EQ(decoder.GetFps(), 30.0);
    XER_UI_EXPECT_EQ(decoder.GetPositionSeconds(), -1.0);

    const long long total = DrainAudio(&decoder);
    XER_UI_EXPECT_TRUE(total > 20000);  // 1 s @ 48 kHz = 48000 samples
    XER_UI_EXPECT_TRUE(decoder.AudioDrained());
}

// Contract: with the tap enabled, AudioDrained() must reach true even
// when the container has no audio stream at all — ReadAudioFrames() has
// no codec to decode with, but the consumer loop must still terminate.
XER_UI_TEST(ffmpeg_decoder_tap_without_audio_stream_drains) {
    if (!XER::Bridge::FFmpeg::IsAvailable()) {
        return;
    }
    std::string err;
    const auto path = (TempDir() / "video_no_audio.mp4").string();
    if (!EncodeTinyVideo(path, &err)) {
        return;  // no video encoder — skip.
    }

    XER::Bridge::FFmpeg::VideoDecoder decoder;
    decoder.EnableAudioTap();
    XER_UI_EXPECT_TRUE(decoder.Open(path, &err));
    if (!decoder.IsOpen()) return;
    XER_UI_EXPECT_FALSE(decoder.HasAudio());

    // Alternate NextFrame()/ReadAudioFrames() until AudioDrained() — the
    // documented consumer contract. ReadAudioFrames() returns false (no
    // audio codec) but must not prevent termination.
    std::vector<std::uint8_t> bgra;
    int w = 0;
    int h = 0;
    std::vector<XER::Bridge::FFmpeg::AudioFrameInfo> chunk;
    int guard = 0;
    while (!decoder.AudioDrained() && guard++ < 100000) {
        decoder.NextFrame(bgra, w, h);
        decoder.ReadAudioFrames(&chunk, 64);
        chunk.clear();
    }
    XER_UI_EXPECT_TRUE(decoder.AudioDrained());
}

// Corrupt-input contract: a WAV truncated mid-data still opens (header
// intact) and the tap must drain rather than spin — exercises the
// demux-exhaustion path consumers rely on for damaged files.
XER_UI_TEST(ffmpeg_decoder_audio_only_truncated_still_drains) {
    if (!XER::Bridge::FFmpeg::IsAvailable()) {
        return;
    }
    std::string err;
    const auto path =
        (TempDir() / "audio_only_truncated.wav").string();
    if (!EncodeWavFile(path, kSampleRate * 2, &err)) {
        XER_UI_EXPECT_TRUE(false);
        return;
    }
    // Chop the file mid-data: the WAV header survives so Open() still
    // succeeds, but the demuxer hits the physical end of file early.
    const auto fsize = std::filesystem::file_size(path);
    std::filesystem::resize_file(path, fsize * 3 / 5);

    XER::Bridge::FFmpeg::VideoDecoder decoder;
    decoder.EnableAudioTap();
    XER_UI_EXPECT_TRUE(decoder.Open(path, &err));
    if (!decoder.IsOpen()) return;
    XER_UI_EXPECT_TRUE(decoder.HasAudio());

    const long long total = DrainAudio(&decoder);
    XER_UI_EXPECT_TRUE(total > 0);  // ~60% of the 2 s fixture survives
    XER_UI_EXPECT_TRUE(decoder.AudioDrained());
}
