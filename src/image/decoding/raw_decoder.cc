#include "raw_decoder.h"
#include "wrapper/ffmpeg/ffmpeg_engine_bridge.h"
#include "flux/core/logger.h"
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace image {

bool RawDecoder::Probe(const uint8_t* h, std::size_t len) const {
    // DNG/NEF/ARW all wrap in TIFF container: 49 49 2A 00
    return len >= 4 && h[0]==0x49 && h[1]==0x49 && h[2]==0x2A && h[3]==0x00;
}

std::shared_ptr<ImageBuffer> RawDecoder::Decode(const std::string& path,
                                                  ImageDescriptor& desc) {
    if (!XER::Bridge::FFmpeg::IsAvailable()) return nullptr;
    XER::Bridge::FFmpeg::MediaInfo info;
    std::string err;
    if (!XER::Bridge::FFmpeg::ProbeMedia(path, &info, &err)) {
        XER::Flux::Core::Logger().Error("RawDecoder", "Probe failed: " + err);
        return nullptr;
    }
    std::vector<XER::Bridge::FFmpeg::VideoFrameInfo> frames;
    if (!XER::Bridge::FFmpeg::DecodeVideoFrames(path, 0, 1, &frames, &err) ||
        frames.empty()) {
        XER::Flux::Core::Logger().Error("RawDecoder", "Decode failed: " + err);
        return nullptr;
    }
    auto& fr = frames[0];
    desc.width  = fr.width;
    desc.height = fr.height;
    desc.format_name = "raw";
    desc.format = PixelFormat::RGBA8;

    if (fr.pixel_format == "rgba" || fr.pixel_format == "bgra") {
        auto* d = fr.data.data();
        if (fr.pixel_format == "bgra") return Bgra8ToBuffer(d, fr.width, fr.height);
        return Rgba8ToBuffer(d, fr.width, fr.height);
    }
    return Rgb24ToBuffer(fr.data.data(), fr.width, fr.height);
}

std::shared_ptr<ImageBuffer> RawDecoder::DecodeMemory(const uint8_t* data, std::size_t len,
                                                        ImageDescriptor& desc) {
    // Write to a temp file so the FFmpeg-backed Decode() path can handle it.
    static std::atomic<std::uint64_t> counter{0};
    const auto tmp = std::filesystem::temp_directory_path() /
        ("xer_raw_" + std::to_string(counter.fetch_add(1)) + "_" +
         std::to_string(reinterpret_cast<std::uintptr_t>(data)) + ".raw");
    const std::string tmp_str = tmp.string();

    bool ok = true;
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            return nullptr;
        }
        out.write(reinterpret_cast<const char*>(data),
                  static_cast<std::streamsize>(len));
        ok = out.good();
    }

    std::shared_ptr<ImageBuffer> buf;
    if (ok) {
        buf = Decode(tmp_str, desc);
    }
    std::error_code ec;
    std::filesystem::remove(tmp, ec);
    return buf;
}

}  // namespace image
