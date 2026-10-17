#include "tiff_decoder.h"
#include "wrapper/ffmpeg/ffmpeg_engine_bridge.h"
#include "flux/core/logger.h"
#include <cstring>

namespace image {

int TiffDecoder::PageCount(const std::string& path) {
    if (!engine::bridge::ffmpeg::IsAvailable()) return 0;
    engine::bridge::ffmpeg::MediaInfo info;
    std::string err;
    if (!engine::bridge::ffmpeg::ProbeMedia(path, &info, &err)) return 0;
    for (auto& s : info.streams)
        if (s.media_type == "video") return s.frame_count > 0 ? static_cast<int>(s.frame_count) : 1;
    return 1;
}

std::shared_ptr<ImageBuffer>
TiffDecoder::DecodePage(const std::string& path, int page, ImageDescriptor& desc) {
    if (!engine::bridge::ffmpeg::IsAvailable()) return nullptr;
    std::string err;
    std::vector<engine::bridge::ffmpeg::VideoFrameInfo> frames;
    if (!engine::bridge::ffmpeg::DecodeVideoFrames(path, 0, page+1, &frames, &err) ||
        frames.empty()) {
        flux::core::Logger().Error("TiffDecoder", "Decode failed: " + err);
        return nullptr;
    }
    int idx = std::min(page, static_cast<int>(frames.size())-1);
    auto& fr = frames[idx];
    desc.width = fr.width;  
    desc.height = fr.height;
    desc.format = PixelFormat::RGBA8; 
    desc.format_name = "tiff";
    auto buf = std::make_shared<ImageBuffer>(fr.width, fr.height, PixelFormat::RGBA8);
    uint8_t* dst = buf->Data();
    const uint8_t* d = fr.data.data();
    if (fr.pixel_format == "rgba") {
        std::memcpy(dst, d, static_cast<size_t>(fr.width)*fr.height*4);
    } else if (fr.pixel_format == "bgra") {
        for (int i=0,n=fr.width*fr.height;i<n;++i) {
            dst[i*4+0]=d[i*4+2]; dst[i*4+1]=d[i*4+1];
            dst[i*4+2]=d[i*4+0]; dst[i*4+3]=d[i*4+3];
        }
    } else {
        for (int i=0,n=fr.width*fr.height;i<n;++i) {
            dst[i*4+0]=d[i*3+0]; dst[i*4+1]=d[i*3+1];
            dst[i*4+2]=d[i*3+2]; dst[i*4+3]=255;
        }
    }
    return buf;
}

}  // namespace image
