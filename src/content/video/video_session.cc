#include "video_session.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <utility>

#include "wrapper/ffmpeg/ffmpeg_video_decoder.h"

namespace XER::Content::Video {

namespace {

constexpr double kSeekSlopSeconds = 0.10;   // accept frames this close to target
constexpr int kSeekMaxDiscardFrames = 120;  // bound the post-seek decode-forward
constexpr double kMinFps = 1.0;
constexpr double kMaxFps = 240.0;

}  // namespace

const char* VideoStateName(VideoState state) {
    switch (state) {
        case VideoState::kIdle: return "idle";
        case VideoState::kOpening: return "opening";
        case VideoState::kReady: return "ready";
        case VideoState::kPlaying: return "playing";
        case VideoState::kPaused: return "paused";
        case VideoState::kEnded: return "ended";
        case VideoState::kError: return "error";
    }
    return "unknown";
}

VideoSession::VideoSession() = default;

VideoSession::~VideoSession() { Close(); }

bool VideoSession::Open(const std::string& path, std::string* error_out) {
    Close();
    if (path.empty()) {
        if (error_out != nullptr) *error_out = "empty path";
        return false;
    }
    if (!ProbeVideo(path, &info_, error_out)) {
        return false;
    }
    path_ = path;
    position_.store(0.0, std::memory_order_relaxed);
    duration_.store(info_.duration_seconds, std::memory_order_relaxed);
    play_requested_.store(false, std::memory_order_relaxed);
    pending_seek_.store(-1.0, std::memory_order_relaxed);

    running_.store(true, std::memory_order_release);
    PublishState(VideoState::kOpening);
    worker_ = std::thread([this] { DecodeLoop(); });
    return true;
}

void VideoSession::Close() {
    running_.store(false, std::memory_order_release);
    control_cv_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
    play_requested_.store(false, std::memory_order_relaxed);
    pending_seek_.store(-1.0, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(frame_mutex_);
        latest_frame_.reset();
    }
    path_.clear();
    info_ = VideoInfo{};
    position_.store(-1.0, std::memory_order_relaxed);
    duration_.store(0.0, std::memory_order_relaxed);
    width_.store(0, std::memory_order_relaxed);
    height_.store(0, std::memory_order_relaxed);
    has_audio_.store(false, std::memory_order_relaxed);
    PublishState(VideoState::kIdle);
}

void VideoSession::Play() {
    const VideoState s = GetState();
    if (s == VideoState::kIdle || s == VideoState::kError) {
        return;
    }
    if (s == VideoState::kOpening) {
        // Remember the request — DecodeLoop picks it up after the first
        // frame lands.
        play_requested_.store(true, std::memory_order_relaxed);
        return;
    }
    if (s == VideoState::kEnded) {
        pending_seek_.store(0.0, std::memory_order_relaxed);
    }
    play_requested_.store(true, std::memory_order_relaxed);
    control_cv_.notify_all();
}

void VideoSession::Pause() {
    play_requested_.store(false, std::memory_order_relaxed);
    const VideoState s = GetState();
    if (s == VideoState::kPlaying || s == VideoState::kReady) {
        PublishState(VideoState::kPaused);
    }
}

void VideoSession::Stop() {
    play_requested_.store(false, std::memory_order_relaxed);
    pending_seek_.store(0.0, std::memory_order_relaxed);
    const VideoState s = GetState();
    if (s == VideoState::kPlaying || s == VideoState::kEnded) {
        PublishState(VideoState::kPaused);
    }
    control_cv_.notify_all();
}

void VideoSession::Seek(double seconds) {
    const VideoState s = GetState();
    if (s == VideoState::kIdle || s == VideoState::kError ||
        s == VideoState::kOpening) {
        return;
    }
    const double d = GetDuration();
    const double clamped =
        d > 0.0 ? std::clamp(seconds, 0.0, d) : std::max(0.0, seconds);
    // Latest seek wins — coalesces slider drags.
    pending_seek_.store(clamped, std::memory_order_relaxed);
    control_cv_.notify_all();
}

void VideoSession::SetLoop(bool loop) {
    loop_.store(loop, std::memory_order_relaxed);
}

double VideoSession::GetPosition() const {
    return position_.load(std::memory_order_relaxed);
}

double VideoSession::GetDuration() const {
    const double d = duration_.load(std::memory_order_relaxed);
    return d > 0.0 ? d : info_.duration_seconds;
}

double VideoSession::GetFps() const {
    return fps_.load(std::memory_order_relaxed);
}

int VideoSession::GetWidth() const {
    return width_.load(std::memory_order_relaxed);
}

int VideoSession::GetHeight() const {
    return height_.load(std::memory_order_relaxed);
}

bool VideoSession::PollFrame(VideoFramePtr* out_frame) {
    if (out_frame == nullptr) return false;
    std::lock_guard<std::mutex> lock(frame_mutex_);
    if (latest_frame_ == nullptr) return false;
    *out_frame = latest_frame_;
    return true;
}

void VideoSession::PublishState(VideoState state) {
    state_.store(state, std::memory_order_release);
    if (state_cb_) {
        state_cb_(state);
    }
    if (event_cb_) {
        VideoEvent event;
        event.type = VideoEvent::Type::kState;
        event.state = state;
        event_cb_(event);
    }
}

void VideoSession::Fail(const std::string& message) {
    play_requested_.store(false, std::memory_order_relaxed);
    PublishState(VideoState::kError);
    if (error_cb_) {
        error_cb_(message);
    }
    if (event_cb_) {
        VideoEvent event;
        event.type = VideoEvent::Type::kError;
        event.message = message;
        event_cb_(event);
    }
}

void VideoSession::DecodeLoop() {
    // Heap + shared_ptr: the audio tap consumer (VideoAudioPlayer) holds a
    // reference via GetAvDecoder() and must outlive this loop's frames.
    auto decoder = std::make_shared<XER::engine::bridge::ffmpeg::VideoDecoder>();
    decoder->EnableAudioTap(true);
    std::string error;
    if (!decoder->Open(path_, &error)) {
        Fail("cannot open '" + path_ + "': " + error);
        return;
    }
    // Publish for the audio consumer as soon as the stream is open.
    av_decoder_.store(decoder, std::memory_order_release);

    fps_.store(std::clamp(decoder->GetFps(), kMinFps, kMaxFps),
               std::memory_order_relaxed);
    const double dur = decoder->GetDurationSeconds();
    if (dur > 0.0) {
        duration_.store(dur, std::memory_order_relaxed);
    }
    width_.store(decoder->GetWidth(), std::memory_order_relaxed);
    height_.store(decoder->GetHeight(), std::memory_order_relaxed);
    has_audio_.store(decoder->HasAudio(), std::memory_order_relaxed);

    std::vector<std::uint8_t> bgra;
    int w = 0, h = 0;
    std::uint64_t decoded_count = 0;

    // Publishes one decoded frame into the mailbox and updates position.
    // `pts` is the frame's stream timestamp; when the stream carries no
    // timestamps we estimate from the decoded-frame count instead.
    const auto publish = [&](double pts) {
        ++decoded_count;
        auto frame = std::make_shared<VideoFrame>();
        frame->bgra = std::move(bgra);
        bgra.clear();
        frame->width = w;
        frame->height = h;
        frame->position_seconds =
            pts >= 0.0 ? pts
                       : static_cast<double>(decoded_count - 1) /
                             fps_.load(std::memory_order_relaxed);
        position_.store(frame->position_seconds,
                        std::memory_order_relaxed);
        frame->sequence = next_sequence_++;
        VideoFramePtr published;
        {
            std::lock_guard<std::mutex> lock(frame_mutex_);
            latest_frame_ = frame;
            published = std::move(frame);
        }
        if (event_cb_) {
            VideoEvent event;
            event.type = VideoEvent::Type::kFrame;
            event.frame = std::move(published);
            event_cb_(event);
        }
    };

    // Pre-decode the first frame so kReady already shows a picture.
    if (decoder->NextFrame(bgra, w, h)) {
        publish(decoder->GetPositionSeconds());
    }

    PublishState(play_requested_.load(std::memory_order_relaxed)
                     ? VideoState::kPlaying
                     : VideoState::kReady);

    auto next_tick = std::chrono::steady_clock::now();

    while (running_.load(std::memory_order_acquire)) {
        // Transport gate: sleep while paused; wake on play/seek/stop.
        {
            std::unique_lock<std::mutex> lock(control_mutex_);
            control_cv_.wait(lock, [this] {
                return !running_.load(std::memory_order_acquire) ||
                       play_requested_.load(std::memory_order_relaxed) ||
                       pending_seek_.load(std::memory_order_relaxed) >= 0.0;
            });
            if (!running_.load(std::memory_order_acquire)) break;
        }

        // Pending seek wins over everything else — works while paused too.
        const double seek_target =
            pending_seek_.exchange(-1.0, std::memory_order_relaxed);
        if (seek_target >= 0.0) {
            if (decoder->Seek(seek_target)) {
                position_.store(seek_target, std::memory_order_relaxed);
                // av_seek_frame lands on the nearest preceding keyframe —
                // decode forward until the frame PTS reaches the target,
                // then publish that frame so a paused preview still shows
                // the requested moment.
                for (int i = 0; i < kSeekMaxDiscardFrames; ++i) {
                    if (!decoder->NextFrame(bgra, w, h)) break;
                    const double pos = decoder->GetPositionSeconds();
                    const double eff =
                        pos >= 0.0 ? pos
                                   : seek_target +
                                         static_cast<double>(i) /
                                             fps_.load(
                                                 std::memory_order_relaxed);
                    if (pos >= seek_target - kSeekSlopSeconds ||
                        i == kSeekMaxDiscardFrames - 1) {
                        publish(pos >= 0.0 ? pos : eff);
                        break;
                    }
                }
            }
            next_tick = std::chrono::steady_clock::now();
            continue;  // re-enter the gate: paused stays paused after seek
        }

        if (!play_requested_.load(std::memory_order_relaxed)) {
            continue;  // paused — wait again
        }

        if (!decoder->NextFrame(bgra, w, h)) {
            // End of stream.
            if (loop_.load(std::memory_order_relaxed) &&
                decoder->Seek(0.0)) {
                position_.store(0.0, std::memory_order_relaxed);
                next_tick = std::chrono::steady_clock::now();
                continue;
            }
            play_requested_.store(false, std::memory_order_relaxed);
            PublishState(VideoState::kEnded);
            continue;  // wait for transport input
        }

        publish(decoder->GetPositionSeconds());

        // Pace to the stream frame rate; if we fell behind, don't burst.
        const double fps = std::clamp(
            fps_.load(std::memory_order_relaxed), kMinFps, kMaxFps);
        next_tick += std::chrono::microseconds(
            static_cast<std::int64_t>(1000000.0 / fps));
        const auto now = std::chrono::steady_clock::now();
        if (next_tick < now) {
            next_tick = now;
        } else {
            std::this_thread::sleep_until(next_tick);
        }
    }

    // Release the shared decoder handle — the audio consumer may still hold
    // a reference; it stops getting packets once IsOpen() goes false.
    av_decoder_.store(nullptr, std::memory_order_release);
    PublishState(VideoState::kIdle);
}

}  // namespace XER::Content::Video
