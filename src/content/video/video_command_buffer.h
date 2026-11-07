#pragma once

#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

#include "video_frame.h"
#include "video_session.h"

namespace XER::Content::Video {

// Command flowing through the VideoCommandBuffer — the decoupling layer
// between decode/audio worker threads and the UI/render thread.
enum class VideoCommandType {
    kFrame,        // carries `frame` (newest wins — older frames coalesce)
    kState,        // carries `state`
    kError,        // carries `message`
    kAudioReady,   // the audio sink finished loading
    kAudioError,   // carries `message`
};

struct VideoCommand {
    VideoCommandType type = VideoCommandType::kState;
    VideoFramePtr frame;
    VideoState state = VideoState::kIdle;
    std::string message;
};

// Multi-producer / single-consumer command queue. Frame commands are
// coalesced: pushing a kFrame replaces an undrained kFrame so the consumer
// always sees the newest picture and the queue can never grow unboundedly
// behind a slow renderer. Everything else keeps FIFO order.
class VideoCommandBuffer {
public:
    // Producer side (any thread).
    void Push(VideoCommand command);
    void PushFrame(VideoFramePtr frame);
    void PushState(VideoState state);
    void PushError(std::string message);
    void PushAudioReady();
    void PushAudioError(std::string message);

    // Consumer side (UI thread): moves every pending command out.
    std::size_t Drain(std::vector<VideoCommand>* out);
    void Clear();
    std::size_t Pending() const;

private:
    mutable std::mutex mutex_;
    std::deque<VideoCommand> queue_;
    static constexpr std::size_t kMaxQueued = 128;
};

}  // namespace XER::Content::Video
