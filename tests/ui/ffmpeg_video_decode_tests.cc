#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "test_harness.h"
#include "wrapper/ffmpeg/ffmpeg_engine_bridge.h"
#include "wrapper/ffmpeg/ffmpeg_video_decoder.h"

namespace {

std::filesystem::path TempDir() {
    const auto dir = std::filesystem::temp_directory_path() /
                     "xer_ui_tests_video";
    std::filesystem::create_directories(dir);
    return dir;
}

// Solid BGRA frame with a moving vertical bar — enough motion for the
// encoder to keep real frames instead of dropping everything as static.
void FillFrame(XER::Bridge::FFmpeg::VideoFrameInfo* frame, int index,
               int w, int h) {
    frame->width = w;
    frame->height = h;
    frame->pixel_format = "bgra";
    frame->line_sizes = {w * 4, 0, 0, 0};
    frame->data.assign(static_cast<size_t>(w) * h * 4u, 0);
    const int bar_x = (index * 5) % w;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const bool bar = x >= bar_x && x < bar_x + 24;
            const size_t o = (static_cast<size_t>(y) * w + x) * 4u;
            frame->data[o + 0] = bar ? 40 : 30;    // B
            frame->data[o + 1] = bar ? 120 : 60;   // G
            frame->data[o + 2] = bar ? 220 : 90;   // R
            frame->data[o + 3] = 255;              // A
        }
    }
}

} // namespace

// Regression: the decoder's send/receive loop used to report a premature
// end-of-stream once the codec's reorder buffer still held output
// (avcodec_send_packet EAGAIN misread as EOF). Playback then looped at
// roughly the buffer depth — ~2 s on affected files. This test encodes a
// 5 s clip with B-frames (libx264) and asserts every frame decodes and the
// reported position reaches the real duration.
XER_UI_TEST(ffmpeg_video_decoder_decodes_full_duration) {
    if (!XER::Bridge::FFmpeg::IsAvailable()) {
        return; // FFmpeg is optional — skip rather than fail.
    }
    const auto path = (TempDir() / "decode_full.mp4").string();

    constexpr int kW = 320;
    constexpr int kH = 240;
    constexpr int kFrames = 150;  // 5 s @ 30 fps
    constexpr int kFps = 30;

    std::string err;
    XER::Bridge::FFmpeg::EncodeVideoParams params;
    params.output_path = path;
    params.width = kW;
    params.height = kH;
    params.fps_num = kFps;
    params.fps_den = 1;
    params.bit_rate = 800000;

    // Prefer libx264 — its default B-frame reorder is what triggered the
    // bug. Fall back to mpeg4 (no B-frames) so the decode path is still
    // exercised on minimal FFmpeg builds.
    bool encoded = false;
    for (const char* codec : {"libx264", "mpeg4"}) {
        params.codec_name = codec;
        err.clear();
        encoded = XER::Bridge::FFmpeg::EncodeVideoFrameStream(
            params, kFrames,
            [kW, kH](std::size_t i,
                     XER::Bridge::FFmpeg::VideoFrameInfo* out,
                     std::string* /*provider_err*/) {
                FillFrame(out, static_cast<int>(i), kW, kH);
                return true;
            },
            &err);
        if (encoded) break;
    }
    if (!encoded) {
        return; // No usable encoder in this build — skip, don't fail.
    }

    XER::Bridge::FFmpeg::VideoDecoder decoder;
    XER_UI_EXPECT_TRUE(decoder.Open(path, &err));
    if (!decoder.IsOpen()) return;

    std::vector<std::uint8_t> bgra;
    int w = 0;
    int h = 0;
    int decoded = 0;
    double last_pos = -1.0;
    double max_pos = -1.0;
    while (decoder.NextFrame(bgra, w, h)) {
        ++decoded;
        last_pos = decoder.GetPositionSeconds();
        if (last_pos > max_pos) max_pos = last_pos;
        if (decoded > kFrames + 60) break;  // safety bound
    }

    // Encoder delay may drop a couple of boundary frames; the old bug cut
    // the stream at the reorder depth (~2 s ≈ 60 frames here).
    XER_UI_EXPECT_TRUE(decoded >= kFrames - 8);
    XER_UI_EXPECT_TRUE(max_pos > 4.5);
    XER_UI_EXPECT_TRUE(last_pos > 4.5);
}
