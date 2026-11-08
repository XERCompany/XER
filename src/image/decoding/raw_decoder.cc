#include "raw_decoder.h"
#include "wrapper/ffmpeg/ffmpeg_engine_bridge.h"
#include "flux/core/logger.h"
#include <cstring>
#include <cstdio>
#include <unistd.h>

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
        flux::core::Logger().Error("RawDecoder", "Probe failed: " + err);
        return nullptr;
    }
    std::vector<XER::Bridge::FFmpeg::VideoFrameInfo> frames;
    if (!XER::Bridge::FFmpeg::DecodeVideoFrames(path, 0, 1, &frames, &err) ||
        frames.empty()) {
        flux::core::Logger().Error("RawDecoder", "Decode failed: " + err);
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
    char tmpl[] = "/tmp/xer_raw_XXXXXX.raw";
    const int fd = mkstemps(tmpl, 4);   // suffix length = 4 (".raw")
    if (fd < 0) return nullptr;

    bool ok = true;
    std::size_t written = 0;
    while (written < len) {
        const ssize_t w = write(fd, data + written,
                                static_cast<size_t>(len - written));
        if (w <= 0) { ok = false; break; }
        written += static_cast<std::size_t>(w);
    }
    close(fd);

    std::shared_ptr<ImageBuffer> buf;
    if (ok) {
        buf = Decode(std::string(tmpl), desc);
    }
    unlink(tmpl);
    return buf;
}

}  // namespace image
