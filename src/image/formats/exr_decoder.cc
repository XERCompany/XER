#include "exr_decoder.h"
#include "wrapper/ffmpeg/ffmpeg_engine_bridge.h"
#include "flux/core/logger.h"
#include <cstring>

namespace image {

std::shared_ptr<ImageBuffer>
ExrDecoder::Decode(const std::string& path, ImageDescriptor& desc) {
    // OpenEXR is HDR; decode via FFmpeg, store in float plane too
    if (!XER::Bridge::FFmpeg::IsAvailable()) return nullptr;
    std::string err;
    std::vector<XER::Bridge::FFmpeg::VideoFrameInfo> frames;
    if (!XER::Bridge::FFmpeg::DecodeVideoFrames(path, 0, 1, &frames, &err) ||
        frames.empty()) {
        XER::Flux::Core::Logger().Error("ExrDecoder", "Decode failed: " + err);
        return nullptr;
    }
    auto& fr = frames[0];
    desc.width   = fr.width;  desc.height = fr.height;
    desc.format  = PixelFormat::RGBA32F;  desc.format_name = "exr";  desc.is_hdr = true;
    auto buf = std::make_shared<ImageBuffer>();
    buf->Allocate(fr.width, fr.height, PixelFormat::RGBA32F);
    // Convert uint8 output to float normalised [0,1]
    auto& fdata = buf->FloatData();
    fdata.resize(static_cast<size_t>(fr.width) * fr.height * 4);
    const uint8_t* d = fr.data.data();
    for (int i=0,n=fr.width*fr.height;i<n;++i) {
        fdata[i*4+0] = d[i*4+0] / 255.0f;
        fdata[i*4+1] = d[i*4+1] / 255.0f;
        fdata[i*4+2] = d[i*4+2] / 255.0f;
        fdata[i*4+3] = d[i*4+3] / 255.0f;
    }
    return buf;
}

}  // namespace image
