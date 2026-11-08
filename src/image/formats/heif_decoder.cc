#include "heif_decoder.h"
#include "wrapper/ffmpeg/ffmpeg_engine_bridge.h"
#include "flux/core/logger.h"
#include <cstring>

namespace image {

std::shared_ptr<ImageBuffer>
HeifDecoder::Decode(const std::string& path, ImageDescriptor& desc) {
    if (!XER::Bridge::FFmpeg::IsAvailable()) return nullptr;
    std::string err;
    std::vector<XER::Bridge::FFmpeg::VideoFrameInfo> frames;
    if (!XER::Bridge::FFmpeg::DecodeVideoFrames(path, 0, 1, &frames, &err) ||
        frames.empty()) {
        flux::core::Logger().Error("HeifDecoder", "Decode failed: " + err);
        return nullptr;
    }
    auto& fr = frames[0];
    desc.width = fr.width;  desc.height = fr.height;
    desc.format = PixelFormat::RGBA8;  desc.format_name = "heif";
    auto buf = std::make_shared<ImageBuffer>(fr.width, fr.height, PixelFormat::RGBA8);
    uint8_t* dst = buf->Data();
    const uint8_t* d = fr.data.data();
    if (fr.pixel_format == "bgra") {
        for (int i=0,n=fr.width*fr.height;i<n;++i) {
            dst[i*4+0]=d[i*4+2]; dst[i*4+1]=d[i*4+1];
            dst[i*4+2]=d[i*4+0]; dst[i*4+3]=d[i*4+3];
        }
    } else if (fr.pixel_format == "rgba") {
        std::memcpy(dst, d, static_cast<size_t>(fr.width)*fr.height*4);
    } else {
        for (int i=0,n=fr.width*fr.height;i<n;++i) {
            dst[i*4+0]=d[i*3+0]; dst[i*4+1]=d[i*3+1];
            dst[i*4+2]=d[i*3+2]; dst[i*4+3]=255;
        }
    }
    return buf;
}

}  // namespace image
