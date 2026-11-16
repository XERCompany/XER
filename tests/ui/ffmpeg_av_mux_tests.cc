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
constexpr std::size_t kAudioOnlyChunks = 71;  // ~1.5 s: 71*1024/48000

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

// ---- EncodeAudioStream (audio-only streaming encode) ----
// ~1.5 s stereo 48 kHz sine in <=1024-sample chunks through the new
// streaming API -> ProbeMedia shows exactly one AUDIO stream; decode a
// few frames via DecodeAudioFrames and check sample_rate/channels.
XER_UI_TEST(ffmpeg_encode_audio_stream_muxes_audio_only) {
    if (!XER::Bridge::FFmpeg::IsAvailable()) {
        return;
    }
    const auto path = (TempDir() / "audio_only.mp4").string();
    std::string err;

    XER::Bridge::FFmpeg::EncodeAudioParams params;
    params.output_path = path;
    params.codec_name = "aac";
    params.sample_rate = kRate;
    params.channels = 2;
    params.sample_format = "s16";
    params.bit_rate = 128000;

    // 71 chunks x 1024 samples = 72704 samples ~= 1.51 s @ 48 kHz.
    // The counting wrapper pins the provider contract: indices
    // [0, frame_count) are each requested exactly once.
    std::vector<int> seen(kAudioOnlyChunks, 0);
    auto provider = [&](std::size_t index,
                        XER::Bridge::FFmpeg::AudioFrameInfo* out,
                        std::string* perr) {
        if (index >= kAudioOnlyChunks) {
            *perr = "provider index out of range";
            return false;
        }
        ++seen[index];
        return AudioProvider(index, out, perr);
    };

    XER_UI_EXPECT_TRUE(XER::Bridge::FFmpeg::EncodeAudioStream(
        params, kAudioOnlyChunks, provider, &err));
    for (std::size_t i = 0; i < kAudioOnlyChunks; ++i) {
        XER_UI_EXPECT_EQ(seen[i], 1);
    }

    XER::Bridge::FFmpeg::MediaInfo info;
    XER_UI_EXPECT_TRUE(XER::Bridge::FFmpeg::ProbeMedia(path, &info, &err));
    XER_UI_EXPECT_EQ(info.stream_count, 1);
    XER_UI_EXPECT_EQ(info.streams.front().media_type, "audio");

    std::vector<XER::Bridge::FFmpeg::AudioFrameInfo> aframes;
    XER_UI_EXPECT_TRUE(XER::Bridge::FFmpeg::DecodeAudioFrames(
        path, info.streams.front().index, 8, &aframes, &err));
    XER_UI_EXPECT_TRUE(!aframes.empty());
    if (!aframes.empty()) {
        XER_UI_EXPECT_EQ(aframes.front().sample_rate, kRate);
        XER_UI_EXPECT_EQ(aframes.front().channels, 2);
        XER_UI_EXPECT_TRUE(aframes.front().sample_count > 0);
    }
}

// frame_count == 0 -> false + non-empty error (mirrors the
// video_frame_count==0 contract of EncodeAudioVideoStream).
XER_UI_TEST(ffmpeg_encode_audio_stream_rejects_empty) {
    if (!XER::Bridge::FFmpeg::IsAvailable()) {
        return;
    }
    const auto path = (TempDir() / "audio_empty.mp4").string();
    std::string err;
    XER::Bridge::FFmpeg::EncodeAudioParams params;
    params.output_path = path;
    params.codec_name = "aac";
    params.sample_rate = kRate;
    params.channels = 2;
    params.sample_format = "s16";
    XER_UI_EXPECT_FALSE(XER::Bridge::FFmpeg::EncodeAudioStream(
        params, 0, AudioProvider, &err));
    XER_UI_EXPECT_TRUE(!err.empty());
}

// Provider dies at chunk 3 -> false; file may exist partially (do NOT
// assert absence — atomic cleanup is the caller's job).
XER_UI_TEST(ffmpeg_encode_audio_stream_provider_error) {
    if (!XER::Bridge::FFmpeg::IsAvailable()) {
        return;
    }
    const auto path = (TempDir() / "audio_fail.mp4").string();
    std::string err;
    XER::Bridge::FFmpeg::EncodeAudioParams params;
    params.output_path = path;
    params.codec_name = "aac";
    params.sample_rate = kRate;
    params.channels = 2;
    params.sample_format = "s16";

    auto bad_audio = [](std::size_t index,
                        XER::Bridge::FFmpeg::AudioFrameInfo* out,
                        std::string* perr) {
        if (index >= 3) {
            *perr = "injected provider failure";
            return false;
        }
        return AudioProvider(index, out, perr);
    };
    XER_UI_EXPECT_FALSE(XER::Bridge::FFmpeg::EncodeAudioStream(
        params, kAudioOnlyChunks, bad_audio, &err));
    XER_UI_EXPECT_TRUE(!err.empty());
}

// Consistency: chunk 5 reports a different sample_rate -> false
// ("requires a consistent audio stream" via FeedAudioEncoderFrame).
XER_UI_TEST(ffmpeg_encode_audio_stream_rejects_inconsistent_format) {
    if (!XER::Bridge::FFmpeg::IsAvailable()) {
        return;
    }
    const auto path = (TempDir() / "audio_inconsistent.mp4").string();
    std::string err;
    XER::Bridge::FFmpeg::EncodeAudioParams params;
    params.output_path = path;
    params.codec_name = "aac";
    params.sample_rate = kRate;
    params.channels = 2;
    params.sample_format = "s16";

    auto inconsistent = [](std::size_t index,
                           XER::Bridge::FFmpeg::AudioFrameInfo* out,
                           std::string* perr) {
        if (!AudioProvider(index, out, perr)) {
            return false;
        }
        if (index == 5) {
            out->sample_rate = kRate + 1;
        }
        return true;
    };
    XER_UI_EXPECT_FALSE(XER::Bridge::FFmpeg::EncodeAudioStream(
        params, kAudioOnlyChunks, inconsistent, &err));
    XER_UI_EXPECT_TRUE(err.find("consistent") != std::string::npos);
}
