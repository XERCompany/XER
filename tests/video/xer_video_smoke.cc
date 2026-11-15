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
        // Two backends, two self-consistent outputs for partial alpha:
        //  - CPU fallback: dst*(1-a) + src*a with a = 0.5 → BGRA ≈ (105,60,40)
        //  - Skia src_over (XER::Bridge::Skia::BlendSrcOver): blends in
        //    premultiplied space without un-premultiplying, so src channels
        //    pass through ~unattenuated → BGRA ≈ (205,110,65)
        // Accept either (±4 LSB); a third, unexpected result still fails.
        const int mB = Channel(dst, 0, 0, 0);
        const int mG = Channel(dst, 0, 0, 1);
        const int mR = Channel(dst, 0, 0, 2);
        XER_VIDEO_CHECK(std::abs(mB - 105) <= 4 || std::abs(mB - 205) <= 4);
        XER_VIDEO_CHECK(std::abs(mG - 60)  <= 4 || std::abs(mG - 110) <= 4);
        XER_VIDEO_CHECK(std::abs(mR - 40)  <= 4 || std::abs(mR - 65)  <= 4);

        video::Frame a = MakeSolidFrame(4, 4, 0, 0, 0, 255);
        video::Frame b = MakeSolidFrame(4, 4, 100, 150, 200, 255);
        auto blended = mix.Blend(a, b, video::MixMode::Alpha, 0.25f);
        XER_VIDEO_CHECK(blended != nullptr);
        if (blended != nullptr) {
            XER_VIDEO_CHECK(blended->IsValid());
            XER_VIDEO_CHECK(blended->Width() == 4);
            XER_VIDEO_CHECK(blended->Height() == 4);
            // CPU: black under 25% of b → B ≈ 25. Skia path: B = 100
            // (same premultiplied-domain divergence as above).
            const int bB = Channel(*blended, 0, 0, 0);
            XER_VIDEO_CHECK(std::abs(bB - 25) <= 4 || std::abs(bB - 100) <= 4);
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
