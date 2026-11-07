#include "video_engine.h"

#include <chrono>
#include <cstdio>
#include <utility>

namespace XER::Content::Video {

VideoEngine::VideoEngine() {
    // Session events → command buffer (worker thread → UI thread).
    session_.SetEventCallback([this](const VideoEvent& event) {
        switch (event.type) {
            case VideoEvent::Type::kFrame:
                commands_.PushFrame(event.frame);
                break;
            case VideoEvent::Type::kState:
                commands_.PushState(event.state);
                break;
            case VideoEvent::Type::kError:
                commands_.PushError(event.message);
                break;
        }
    });
    // Audio sink readiness → command buffer.
    audio_.SetReadyCallback([this](bool ok, const std::string& message) {
        if (ok) {
            commands_.PushAudioReady();
        } else {
            commands_.PushAudioError(message);
        }
    });
}

VideoEngine::~VideoEngine() { Close(); }

bool VideoEngine::Open(const std::string& path, std::string* error_out) {
    Close();
    commands_.Clear();
    current_path_ = path;
    poster_stored_ = false;
    audio_ready_ = false;
    audio_failed_ = false;
    audio_attached_ = false;
    want_playing_ = false;
    latest_frame_.reset();
    frame_seq_ = 0;
    ui_state_ = VideoState::kIdle;
    status_text_.clear();

    if (!session_.Open(path, error_out)) {
        return false;
    }

    // Audio attaches lazily in Tick() once the session publishes its
    // shared decoder — the container is opened exactly once, by the video
    // path; the player streams the audio track off the same demuxer.
    return true;
}

void VideoEngine::Close() {
    audio_.Close();
    session_.Close();
    commands_.Clear();
    audio_attached_ = false;
    latest_frame_.reset();
    frame_seq_ = 0;
    ui_state_ = VideoState::kIdle;
    status_text_.clear();
    audio_ready_ = false;
    audio_failed_ = false;
    want_playing_ = false;
    poster_stored_ = false;
    current_path_.clear();
}

void VideoEngine::Play() {
    want_playing_ = true;
    session_.Play();
    if (audio_ready_) audio_.Play();
}

void VideoEngine::Pause() {
    want_playing_ = false;
    session_.Pause();
    audio_.Pause();
}

void VideoEngine::Stop() {
    want_playing_ = false;
    session_.Stop();
    audio_.Stop();
    audio_.Seek(0.0);
}

void VideoEngine::Seek(double seconds) {
    session_.Seek(seconds);
    audio_.Seek(seconds);
}

void VideoEngine::SetLoop(bool loop) {
    session_.SetLoop(loop);
}

double VideoEngine::GetPosition() const {
    return session_.GetPosition();
}

double VideoEngine::GetDuration() const {
    return session_.GetDuration();
}

double VideoEngine::GetFps() const {
    return session_.GetFps();
}

int VideoEngine::GetWidth() const {
    return session_.GetWidth();
}

int VideoEngine::GetHeight() const {
    return session_.GetHeight();
}

bool VideoEngine::HasAudio() const {
    return session_.HasAudio() || audio_.HasAudio();
}

bool VideoEngine::AudioReady() const {
    return audio_ready_;
}

bool VideoEngine::IsPlaying() const {
    return ui_state_ == VideoState::kPlaying;
}

VideoFramePtr VideoEngine::GetLatestFrame() const {
    return latest_frame_;
}

VideoFramePtr VideoEngine::RecallFrame(const std::string& path) const {
    return frame_cache_.Recall(path);
}

VideoFramePtr VideoEngine::GetCachedPoster(const std::string& path) const {
    auto frame = std::make_shared<VideoFrame>();
    if (frame_cache_.LoadPoster(path, frame.get())) {
        return frame;
    }
    return nullptr;
}

std::string VideoEngine::GetStatusText() const {
    if (!status_text_.empty()) return status_text_;
    const VideoInfo& info = session_.GetInfo();
    if (!info.valid) return VideoStateName(ui_state_);
    char buf[160];
    std::snprintf(buf, sizeof(buf), "%dx%d @ %.0ffps · %s%s · %.1fs",
                  info.width, info.height, GetFps(),
                  info.video_codec.c_str(),
                  info.has_audio ? " + audio" : "",
                  GetDuration());
    return buf;
}

void VideoEngine::HandleCommands() {
    commands_.Drain(&drain_scratch_);
    for (const auto& cmd : drain_scratch_) {
        switch (cmd.type) {
            case VideoCommandType::kFrame: {
                if (cmd.frame != nullptr) {
                    frame_seq_ = cmd.frame->sequence;
                    latest_frame_ = cmd.frame;
                    // Memory cache: keep the newest frame per path so a
                    // tab switch back shows the picture instantly.
                    frame_cache_.Remember(current_path_, cmd.frame);
                    // Persist the poster once — the first decoded frame is
                    // a fine still for instant re-preview.
                    if (!poster_stored_) {
                        poster_stored_ = true;
                        frame_cache_.StorePoster(current_path_, *cmd.frame);
                    }
                }
                break;
            }
            case VideoCommandType::kState: {
                ui_state_ = cmd.state;
                if (cmd.state == VideoState::kError) {
                    status_text_ = "decode error";
                } else if (cmd.state != VideoState::kPlaying) {
                    status_text_.clear();
                }
                // Mirror terminal states into the audio sink.
                if (cmd.state == VideoState::kEnded ||
                    cmd.state == VideoState::kPaused ||
                    cmd.state == VideoState::kReady) {
                    if (cmd.state == VideoState::kEnded) {
                        audio_.Stop();
                    } else if (cmd.state == VideoState::kPaused ||
                               cmd.state == VideoState::kReady) {
                        audio_.Pause();
                    }
                }
                break;
            }
            case VideoCommandType::kError:
                status_text_ = cmd.message;
                break;
            case VideoCommandType::kAudioReady:
                // Worker finished the WAV — attach the player on this
                // (UI) thread, then honor the current play intent.
                if (audio_.PollReady()) {
                    audio_ready_ = true;
                    if (want_playing_ &&
                        ui_state_ == VideoState::kPlaying) {
                        audio_.Play();
                        audio_.Seek(session_.GetPosition());
                    }
                } else {
                    audio_failed_ = true;
                }
                break;
            case VideoCommandType::kAudioError:
                audio_failed_ = true;
                break;
        }
    }
}

void VideoEngine::Tick() {
    HandleCommands();

    // A/V sync: hard-seek the audio when it drifts from the video clock.
    if (ui_state_ == VideoState::kPlaying && audio_ready_) {
        audio_.SyncTo(session_.GetPosition());
    }

    // Attach the audio player to the session's shared decoder once it is
    // published (DecodeLoop stores it right after Open).
    if (!audio_attached_ && !audio_ready_ && !audio_failed_ &&
        session_.GetInfo().has_audio) {
        if (auto decoder = session_.GetAvDecoder()) {
            audio_attached_ = true;
            if (!audio_.Attach(std::move(decoder))) {
                audio_failed_ = true;
            }
        }
    }

    // Retry audio attach while the worker is still extracting.
    if (!audio_ready_ && !audio_failed_ && session_.GetInfo().has_audio) {
        audio_.PollReady();
    }
}

}  // namespace XER::Content::Video
