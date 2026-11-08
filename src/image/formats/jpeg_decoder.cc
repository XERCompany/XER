#include "jpeg_decoder.h"
#include "wrapper/ffmpeg/ffmpeg_engine_bridge.h"
#include "flux/core/logger.h"
#include <cstring>

namespace image {

static std::shared_ptr<ImageBuffer>
DecodeViaFFmpeg(const std::string& path, const std::string& fmt_name,
                ImageDescriptor& desc) {
    if (!XER::Bridge::FFmpeg::IsAvailable()) return nullptr;
    std::string err;
    std::vector<XER::Bridge::FFmpeg::VideoFrameInfo> frames;
    if (!XER::Bridge::FFmpeg::DecodeVideoFrames(path, 0, 1, &frames, &err) ||
        frames.empty()) {
        flux::core::Logger().Error("ImageDecoder", fmt_name + " decode failed: " + err);
        return nullptr;
    }
    auto& fr = frames[0];
    desc.width  = fr.width;  desc.height = fr.height;
    desc.format = PixelFormat::RGBA8;  desc.format_name = fmt_name;
    const uint8_t* d = fr.data.data();
    if (fr.pixel_format == "bgra") {
        auto buf = std::make_shared<ImageBuffer>(fr.width, fr.height, PixelFormat::RGBA8);
        uint8_t* dst = buf->Data();
        for (int i=0,n=fr.width*fr.height;i<n;++i) {
            dst[i*4+0]=d[i*4+2]; dst[i*4+1]=d[i*4+1];
            dst[i*4+2]=d[i*4+0]; dst[i*4+3]=d[i*4+3];
        }
        return buf;
    }
    if (fr.pixel_format == "rgba") {
        auto buf = std::make_shared<ImageBuffer>(fr.width, fr.height, PixelFormat::RGBA8);
        std::memcpy(buf->Data(), d, static_cast<size_t>(fr.width)*fr.height*4);
        return buf;
    }
    // rgb24 fallback
    auto buf = std::make_shared<ImageBuffer>(fr.width, fr.height, PixelFormat::RGBA8);
    uint8_t* dst = buf->Data();
    for (int i=0,n=fr.width*fr.height;i<n;++i) {
        dst[i*4+0]=d[i*3+0]; dst[i*4+1]=d[i*3+1];
        dst[i*4+2]=d[i*3+2]; dst[i*4+3]=255;
    }
    return buf;
}

std::shared_ptr<ImageBuffer> JpegDecoder::Decode(const std::string& path,
                                                   ImageDescriptor& desc) {
    return DecodeViaFFmpeg(path, "jpeg", desc);
}

std::shared_ptr<ImageBuffer> JpegDecoder::DecodeMemory(const uint8_t*, std::size_t,
                                                         ImageDescriptor&) {
    return nullptr;  // FFmpeg bridge is file-based
}

}  // namespace image
