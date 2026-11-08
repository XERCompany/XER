#include <filesystem>
#include <fstream>
#include <string>

#include "test_harness.h"
#include "wrapper/ffmpeg/ffmpeg_engine_bridge.h"

namespace {

void WriteFile(const std::filesystem::path& path, const std::string& data) {
    std::ofstream out(path, std::ios::binary);
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
}

std::filesystem::path TempDir() {
    const auto dir = std::filesystem::temp_directory_path() /
                     "xer_ui_tests_ffmpeg";
    std::filesystem::create_directories(dir);
    return dir;
}

} // namespace

// Smoke test for the project-avatar pipeline: any FFmpeg-decodable image
// in, JPEG out (icons/<id>/icon.jpg).
XER_UI_TEST(ffmpeg_image_to_jpeg_converts_ppm) {
    if (!XER::Bridge::FFmpeg::IsAvailable()) {
        return; // FFmpeg is optional — skip rather than fail.
    }
    const auto dir = TempDir();
    const auto src = dir / "avatar_src.ppm";
    const auto dst = dir / "avatar_out.jpg";

    // 4x4 P6 PPM — trivially decodable by FFmpeg's pnm decoder.
    std::string ppm = "P6\n4 4\n255\n";
    for (int i = 0; i < 16; ++i) {
        ppm += static_cast<char>(0x40 + (i % 4) * 0x30);
        ppm += static_cast<char>(0x80);
        ppm += static_cast<char>(0xC0 - (i % 4) * 0x20);
    }
    WriteFile(src, ppm);

    std::string err;
    XER_UI_EXPECT_TRUE(XER::Bridge::FFmpeg::ImageToJpeg(
        src.string(), dst.string(), 256, &err));
    XER_UI_EXPECT_TRUE(std::filesystem::exists(dst));
    XER_UI_EXPECT_TRUE(std::filesystem::file_size(dst) > 0);

    // JPEG magic bytes: FF D8.
    std::ifstream in(dst, std::ios::binary);
    char magic[2]{};
    in.read(magic, 2);
    XER_UI_EXPECT_EQ(static_cast<int>(static_cast<unsigned char>(magic[0])),
                     0xFF);
    XER_UI_EXPECT_EQ(static_cast<int>(static_cast<unsigned char>(magic[1])),
                     0xD8);
}
