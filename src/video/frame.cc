#include "frame.h"

#include <limits>

namespace video {

Frame::Frame(int w, int h, PixelFormat fmt) {
    Allocate(w, h, fmt);
}

bool Frame::Allocate(int w, int h, PixelFormat fmt) {
    if (w <= 0 || h <= 0) {
        Release();
        return false;
    }

    const auto width = static_cast<size_t>(w);
    const auto height = static_cast<size_t>(h);
    if (width > std::numeric_limits<size_t>::max() / height) {
        Release();
        return false;
    }
    const size_t pixels = width * height;

    meta_.width  = w;
    meta_.height = h;
    meta_.format = fmt;
    size_t sz = 0;
    switch (fmt) {
        case PixelFormat::BGRA8:
        case PixelFormat::RGBA8:
            if (pixels > std::numeric_limits<size_t>::max() / 4) {
                Release();
                return false;
            }
            sz = pixels * 4;
            break;
        case PixelFormat::NV12:
        case PixelFormat::YUV420P:
            if (pixels > (std::numeric_limits<size_t>::max() / 3) * 2) {
                Release();
                return false;
            }
            sz = pixels * 3 / 2;
            break;
    }
    data_.assign(sz, 0);
    return sz > 0;
}

void Frame::Release() {
    data_.clear();
    meta_ = {};
}

int Frame::Stride() const {
    switch (meta_.format) {
        case PixelFormat::BGRA8:
        case PixelFormat::RGBA8: return meta_.width * 4;
        case PixelFormat::NV12:
        case PixelFormat::YUV420P: return meta_.width;
    }
    return meta_.width;
}

}  // namespace video