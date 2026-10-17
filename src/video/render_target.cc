#include "render_target.h"

namespace video {

RenderTarget::RenderTarget(int width, int height, PixelFormat fmt) {
    Resize(width, height, fmt);
}

void RenderTarget::Resize(int width, int height, PixelFormat fmt) {
    if (width <= 0 || height <= 0) {
        width_ = 0;
        height_ = 0;
        fmt_ = fmt;
        frame_.reset();
        return;
    }

    width_  = width;
    height_ = height;
    fmt_    = fmt;
    frame_  = std::make_unique<Frame>();
    if (!frame_->Allocate(width, height, fmt)) frame_.reset();
}

}  // namespace video