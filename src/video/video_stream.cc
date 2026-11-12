#include "video_stream.h"
#include "ffmpeg_frame_source.h"
#include "flux/core/logger.h"

namespace video {

static XER::Flux::Core::Logger& Log() {
    static XER::Flux::Core::Logger logger;
    return logger;
}

VideoStream::VideoStream(std::string id) : id_(std::move(id)) {}
VideoStream::~VideoStream() { Close(); }

void VideoStream::SetSource(std::unique_ptr<FrameSource> src) {
    source_ = std::move(src);
}

bool VideoStream::Open() {
    if (!source_) return false;
    Log().Info("VideoStream", "Opening stream: " + id_);
    bool ok = source_->Open(uri_);
    if (ok) {
        state_ = StreamState::Idle;
        double src_fps = source_->Fps();
        if (src_fps > 0.0) fps_ctrl_.SetTargetFps(src_fps);
        Log().Info("VideoStream",
                   id_ + " opened [" + std::to_string(source_->Width()) +
                   "x" + std::to_string(source_->Height()) +
                   " @ " + std::to_string(static_cast<int>(src_fps)) + " fps]");
    } else {
        state_ = StreamState::Error;
        Log().Error("VideoStream", "Failed to open stream: " + id_);
    }
    return ok;
}

bool VideoStream::OpenUri(const std::string& uri) {
    uri_ = uri;
    if (!source_) source_ = std::make_unique<FFmpegFrameSource>();
    return Open();
}

void VideoStream::Close() {
    if (source_) source_->Close();
    state_ = StreamState::Stopped;
    Log().Debug("VideoStream", "Stream closed: " + id_);
}

void VideoStream::Play()  { state_ = StreamState::Playing;  Log().Debug("VideoStream", id_ + " → Playing");  }
void VideoStream::Pause() { state_ = StreamState::Paused;   Log().Debug("VideoStream", id_ + " → Paused");   }
void VideoStream::Stop()  { state_ = StreamState::Stopped;  Close(); }

std::shared_ptr<Frame> VideoStream::NextFrame() {
    if (state_ != StreamState::Playing || !source_ || !source_->IsOpen())
        return nullptr;
    if (!fps_ctrl_.ShouldRender()) return nullptr;
    fps_ctrl_.WaitNextFrame();
    return source_->NextFrame();
}

}  // namespace video
