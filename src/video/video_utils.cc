#include "video_utils.h"
#include "wrapper/ffmpeg/ffmpeg_engine_bridge.h"
#include "wrapper/skia/skia_engine_bridge.h"

#include <cstring>

namespace video::utils {

namespace {

uint32_t ReadPackedPixel(const uint8_t* src, PixelFormat fmt) {
    if (fmt == PixelFormat::RGBA8) return engine::bridge::skia::MakeColorRGBA(src[0], src[1], src[2], src[3]);
    return engine::bridge::skia::MakeColorRGBA(src[2], src[1], src[0], src[3]);
}

void WritePackedPixel(uint32_t color, PixelFormat fmt, uint8_t* dst) {
    const auto r = static_cast<uint8_t>((color >> 24) & 0xFF);
    const auto g = static_cast<uint8_t>((color >> 16) & 0xFF);
    const auto b = static_cast<uint8_t>((color >> 8) & 0xFF);
    const auto a = static_cast<uint8_t>(color & 0xFF);
    if (fmt == PixelFormat::RGBA8) {
        dst[0] = r;
        dst[1] = g;
        dst[2] = b;
        dst[3] = a;
        return;
    }
    dst[0] = b;
    dst[1] = g;
    dst[2] = r;
    dst[3] = a;
}

}  // namespace

std::string PixelFormatName(PixelFormat fmt) {
    switch (fmt) {
        case PixelFormat::BGRA8:   return "BGRA8";
        case PixelFormat::RGBA8:   return "RGBA8";
        case PixelFormat::NV12:    return "NV12";
        case PixelFormat::YUV420P: return "YUV420P";
    }
    return "Unknown";
}

bool HasAlpha(PixelFormat fmt) {
    return fmt == PixelFormat::BGRA8 || fmt == PixelFormat::RGBA8;
}

int BytesPerPixel(PixelFormat fmt) {
    switch (fmt) {
        case PixelFormat::BGRA8:
        case PixelFormat::RGBA8: return 4;
        default: return 0;
    }
}

bool IsPackedFourChannel(PixelFormat fmt) {
    return BytesPerPixel(fmt) == 4;
}

bool CopyFramePixels(const Frame& src, Frame& dst) {
    if (!src.IsValid() || !dst.IsValid()) return false;
    if (src.Width() != dst.Width() || src.Height() != dst.Height() || src.Format() != dst.Format()) return false;
    if (src.DataSize() != dst.DataSize()) return false;
    std::memcpy(dst.Data(), src.Data(), src.DataSize());
    return true;
}

bool ResizeFrameWithSkia(const Frame& src, Frame& dst) {
    if (!src.IsValid() || !dst.IsValid()) return false;
    if (!IsPackedFourChannel(src.Format()) || !IsPackedFourChannel(dst.Format())) return false;
    if (src.Width() <= 0 || src.Height() <= 0 || dst.Width() <= 0 || dst.Height() <= 0) return false;
    if (!engine::bridge::skia::IsAvailable()) return false;

    const int src_pixels = src.Width() * src.Height();
    const int dst_pixels = dst.Width() * dst.Height();
    std::vector<uint32_t> src_px(src_pixels);
    std::vector<uint32_t> dst_px(dst_pixels, 0);

    const uint8_t* src_data = src.Data();
    for (int i = 0; i < src_pixels; ++i) src_px[i] = ReadPackedPixel(src_data + i * 4, src.Format());

    engine::bridge::skia::RasterDrawImage(&dst_px, dst.Width(), dst.Height(), src_px, src.Width(), src.Height(), 0, 0, "src");

    uint8_t* dst_data = dst.Data();
    for (int i = 0; i < dst_pixels; ++i) WritePackedPixel(dst_px[i], dst.Format(), dst_data + i * 4);
    return true;
}

/// Returns list of codecs from FFmpeg bridge (decoded + encoded names).
std::vector<std::string> AvailableVideoCodecs() {
    std::vector<std::string> result;
    if (!engine::bridge::ffmpeg::IsAvailable()) return result;
    for (const auto& c : engine::bridge::ffmpeg::Codecs()) {
        if (c.media_type == "video") result.push_back(c.name);
    }
    return result;
}

/// Returns FFmpeg MediaInfo for the file at path.
bool ProbeFile(const std::string& path,
               engine::bridge::ffmpeg::MediaInfo* out_info,
               std::string* out_error) {
    if (!engine::bridge::ffmpeg::IsAvailable()) {
        if (out_error) *out_error = "FFmpeg not available";
        return false;
    }
    return engine::bridge::ffmpeg::ProbeMedia(path, out_info, out_error);
}

/// Blend two BGRA8 pixels using Skia BlendModeApply.
uint32_t BlendPixels(const uint8_t* dst_bgra, const uint8_t* src_bgra,
                     const std::string& skia_blend_mode) {
    if (!engine::bridge::skia::IsAvailable()) return 0;
    uint32_t d = engine::bridge::skia::MakeColorRGBA(
                     dst_bgra[2], dst_bgra[1], dst_bgra[0], dst_bgra[3]);
    uint32_t s = engine::bridge::skia::MakeColorRGBA(
                     src_bgra[2], src_bgra[1], src_bgra[0], src_bgra[3]);
    return engine::bridge::skia::BlendModeApply(skia_blend_mode, d, s);
}

}  // namespace video::utils