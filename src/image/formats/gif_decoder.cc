#include "gif_decoder.h"
#include "wrapper/ffmpeg/ffmpeg_engine_bridge.h"
#include "flux/core/logger.h"
#include <cstring>

namespace image {

int GifDecoder::PageCount(const std::string& path) {
    if (!XER::Bridge::FFmpeg::IsAvailable()) return 0;
    XER::Bridge::FFmpeg::MediaInfo info;
    std::string err;
    if (!XER::Bridge::FFmpeg::ProbeMedia(path, &info, &err)) return 0;
    // video stream frame count
    for (auto& s : info.streams)
        if (s.media_type == "video") return s.frame_count > 0 ? static_cast<int>(s.frame_count) : 1;
    return 1;
}

std::shared_ptr<ImageBuffer>
GifDecoder::DecodePage(const std::string& path, int page, ImageDescriptor& desc) {
    if (!XER::Bridge::FFmpeg::IsAvailable()) return nullptr;
    std::string err;
    std::vector<XER::Bridge::FFmpeg::VideoFrameInfo> frames;
    if (!XER::Bridge::FFmpeg::DecodeVideoFrames(path, 0, page+1, &frames, &err) ||
        frames.empty()) {
        flux::core::Logger().Error("GifDecoder", "Decode failed: " + err);
        return nullptr;
    }
    int idx = std::min(page, static_cast<int>(frames.size())-1);
    auto& fr = frames[idx];
    desc.width = fr.width;  desc.height = fr.height;
    desc.format = PixelFormat::RGBA8;  desc.format_name = "gif";
    auto buf = std::make_shared<ImageBuffer>(fr.width, fr.height, PixelFormat::RGBA8);
    uint8_t* dst = buf->Data();
    const uint8_t* d = fr.data.data();
    if (fr.pixel_format == "bgra") {
        for (int i=0,n=fr.width*fr.height;i<n;++i) {
            dst[i*4+0]=d[i*4+2]; dst[i*4+1]=d[i*4+1];
            dst[i*4+2]=d[i*4+0]; dst[i*4+3]=d[i*4+3];
        }
    } else {
        std::memcpy(dst, d, static_cast<size_t>(fr.width)*fr.height*4);
    }
    return buf;
}

}  // namespace image
