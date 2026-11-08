#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "video_frame.h"
#include "video_info.h"

namespace XER::Bridge::FFmpeg {
class VideoDecoder;
}

namespace XER::Content::Video {

// Playback state of a VideoSession.
enum class VideoState {
    kIdle,     // nothing loaded
    kOpening,  // container probe done; decoder open in progress on worker
    kReady,    // open; paused (first frame may already be published)
    kPlaying,
    kPaused,   // open; transport stopped mid-stream
    kEnded,    // reached the end of a non-looping stream
    kError,
};

const char* VideoStateName(VideoState state);

// Async event pushed by the decode worker: newest frame, state transition
// or fatal error. Delivered through SetEventCallback on the worker thread.
struct VideoEvent {
    enum class Type { kFrame, kState, kError };
    Type type = Type::kState;
    VideoFramePtr frame;      // kFrame
    VideoState state = VideoState::kIdle;  // kState
    std::string message;      // kError
};

// Frame-driven video preview/playback session: owns the FFmpeg decoder and
// a worker thread that decodes at the stream frame rate while playing.
// Every public method is thread-safe and may be called from the UI thread.
//
// Frame delivery is a single-slot mailbox: PollFrame() hands out the newest
// decoded frame and stale frames are dropped — renderers never block and
// never queue.
//
// Callbacks (SetStateCallback / SetErrorCallback) fire on the decode
// worker thread — keep them cheap; hop to the UI thread when needed.
class VideoSession {
public:
    VideoSession();
    ~VideoSession();  // stops the worker and joins it

    VideoSession(const VideoSession&) = delete;
    VideoSession& operator=(const VideoSession&) = delete;

    // Probes the file synchronously (container headers only — fast), then
    // hands decoder open + first-frame decode to the worker thread. Returns
    // false only when the probe itself fails; decode errors arrive later
    // through the state callback as kError.
    bool Open(const std::string& path, std::string* error_out = nullptr);
    void Close();
    bool IsOpen() const {
        return state_.load(std::memory_order_acquire) != VideoState::kIdle;
    }

    // Transport — safe from any thread; all are no-ops while kIdle/kError.
    void Play();   // also restarts from kEnded
    void Pause();
    void Stop();   // pause + rewind to 0 (keeps the file open)
    void Seek(double seconds);  // clamped to [0, duration]; frame-accurate
    void SetLoop(bool loop);
    bool IsLooping() const { return loop_.load(std::memory_order_relaxed); }

    // State — thread-safe snapshots.
    VideoState GetState() const {
        return state_.load(std::memory_order_acquire);
    }
    double GetPosition() const;   // seconds; -1 when unknown
    double GetDuration() const;   // seconds; 0 when unknown
    double GetFps() const;
    int GetWidth() const;
    int GetHeight() const;
    bool HasAudio() const { return has_audio_.load(std::memory_order_relaxed); }
    const VideoInfo& GetInfo() const { return info_; }

    // Copies out the newest published frame (non-blocking). Dedup via
    // frame->sequence — polling twice without a new decode returns the
    // same sequence. Returns false while no frame exists yet.
    bool PollFrame(VideoFramePtr* out_frame);

    // The shared FFmpeg decoder once the worker has opened it (nullptr
    // before). Consumers (e.g. the audio player) attach to this instance so
    // audio packets are tapped from the same already-open container — the
    // file is demuxed exactly once for both streams.
    std::shared_ptr<XER::Bridge::FFmpeg::VideoDecoder> GetAvDecoder() const {
        return av_decoder_.load(std::memory_order_acquire);
    }

    using StateCallback = std::function<void(VideoState state)>;
    void SetStateCallback(StateCallback callback) {
        state_cb_ = std::move(callback);
    }
    using ErrorCallback = std::function<void(const std::string& message)>;
    void SetErrorCallback(ErrorCallback callback) {
        error_cb_ = std::move(callback);
    }
    // Unified async event stream (frames + state + errors) for command-
    // buffer style consumers; fired on the decode worker thread.
    using EventCallback = std::function<void(const VideoEvent& event)>;
    void SetEventCallback(EventCallback callback) {
        event_cb_ = std::move(callback);
    }

private:
    void DecodeLoop();
    void PublishState(VideoState state);
    void Fail(const std::string& message);
    // Performs decoder.Seek + decode-discards until the frame PTS reaches
    // the target (keyframe seeks land early by design).
    void ApplySeek(XER::Bridge::FFmpeg::VideoDecoder& decoder,
                   double target_seconds);

    VideoInfo info_;
    std::string path_;

    std::thread worker_;
    std::atomic<bool> running_{false};
    std::atomic<VideoState> state_{VideoState::kIdle};
    std::atomic<bool> play_requested_{false};
    std::atomic<bool> loop_{false};
    std::atomic<double> pending_seek_{-1.0};  // <0 means "no seek queued"
    std::atomic<double> position_{-1.0};
    std::atomic<double> duration_{0.0};
    std::atomic<double> fps_{30.0};
    std::atomic<int> width_{0};
    std::atomic<int> height_{0};
    std::atomic<bool> has_audio_{false};

    // Transport gate: the worker waits here while paused.
    std::mutex control_mutex_;
    std::condition_variable control_cv_;

    // Shared decoder handle published by DecodeLoop for audio consumers.
    std::atomic<std::shared_ptr<XER::Bridge::FFmpeg::VideoDecoder>>
        av_decoder_{nullptr};

    // Single-slot frame mailbox.
    std::mutex frame_mutex_;
    VideoFramePtr latest_frame_;
    std::uint64_t next_sequence_ = 1;

    StateCallback state_cb_;
    ErrorCallback error_cb_;
    EventCallback event_cb_;
};

}  // namespace XER::Content::Video
