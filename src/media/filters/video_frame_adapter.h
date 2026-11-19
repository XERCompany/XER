/**
 * VideoFrameAdapter — conversions between XER::Media::DecodedFrame and
 * video::Frame (src/video). Lets media components interoperate with the
 * video editor's frame types, ClipFrameCache and GPU path.
 */
#pragma once

#include "media/decoders/decoder.h"
#include "video/frame.h"

namespace XER::Media {

inline video::Frame ToVideoFrame(const DecodedFrame& src) {
    video::Frame f;
    f.Allocate(src.width, src.height, video::PixelFormat::BGRA8);
    const std::size_t n = f.DataSize() < src.pixels.size()
                              ? f.DataSize()
                              : src.pixels.size();
    std::memcpy(f.Data(), src.pixels.data(), n);
    f.SetPts(src.timestamp);
    f.Meta().dts = src.timestamp;
    f.Meta().fps = src.duration > 0 ? 1000000.0 / src.duration : 0.0;
    return f;
}

inline DecodedFrame FromVideoFrame(const video::Frame& src) {
    DecodedFrame f;
    f.width = src.Width();
    f.height = src.Height();
    f.timestamp = src.Pts();
    f.pixels.assign(src.Data(), src.Data() + src.DataSize());
    return f;
}

}  // namespace XER::Media
