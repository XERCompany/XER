#include "media/renderers/frame_cache_sink.h"

#include "media/filters/video_frame_adapter.h"
#include "video/clip_frame_cache.h"

namespace XER::Media {

FrameCacheSink::FrameCacheSink(std::filesystem::path root, std::string key)
    : cache_(std::make_unique<video::ClipFrameCache>(std::move(root),
                                                   std::move(key))) {}

FrameCacheSink::~FrameCacheSink() = default;

void FrameCacheSink::Start(Callback cb) {
    cb_ = std::move(cb);
    running_ = true;
}

void FrameCacheSink::Stop() { running_ = false; }

bool FrameCacheSink::Render(const DecodedFrame& frame,
                            std::string* error_out) {
    if (!running_ || frame.width <= 0 || frame.pixels.empty()) {
        if (error_out) *error_out = "invalid frame or sink stopped";
        return false;
    }
    if (!cache_->WriteFrame(frames_written_, ToVideoFrame(frame),
                            error_out))
        return false;
    ++frames_written_;
    if (cb_) cb_();
    return true;
}

}  // namespace XER::Media
