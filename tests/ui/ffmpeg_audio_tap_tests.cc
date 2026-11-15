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
