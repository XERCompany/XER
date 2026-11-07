#include "video_decoder.h"

#include "wrapper/ffmpeg/ffmpeg_video_decoder.h"

namespace XER::Content::Video {

// All FFmpeg/libyuv usage for video decode lives in src/wrapper/ffmpeg —
// this class is a thin adapter keeping the XER::Content::Video API stable.
struct VideoDecoder::Impl {
    XER::engine::bridge::ffmpeg::VideoDecoder decoder;
};

VideoDecoder::VideoDecoder() : impl_(std::make_unique<Impl>()) {}

VideoDecoder::~VideoDecoder() = default;

bool VideoDecoder::Open(const std::string& path, std::string* error_out) {
    return impl_->decoder.Open(path, error_out);
}

void VideoDecoder::Close() {
    impl_->decoder.Close();
}

bool VideoDecoder::IsOpen() const {
    return impl_->decoder.IsOpen();
}

bool VideoDecoder::NextFrame(std::vector<uint8_t>& bgra, int& width,
                             int& height) {
    return impl_->decoder.NextFrame(bgra, width, height);
}

bool VideoDecoder::Seek(double seconds) {
    return impl_->decoder.Seek(seconds);
}

double VideoDecoder::GetPositionSeconds() const {
    return impl_->decoder.GetPositionSeconds();
}

bool VideoDecoder::HasAudio() const {
    return impl_->decoder.HasAudio();
}

double VideoDecoder::GetFps() const {
    return impl_->decoder.GetFps();
}

double VideoDecoder::GetDurationSeconds() const {
    return impl_->decoder.GetDurationSeconds();
}

int VideoDecoder::GetWidth() const {
    return impl_->decoder.GetWidth();
}

int VideoDecoder::GetHeight() const {
    return impl_->decoder.GetHeight();
}

}  // namespace XER::Content::Video
