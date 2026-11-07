#include "video_command_buffer.h"

#include <utility>

namespace XER::Content::Video {

void VideoCommandBuffer::Push(VideoCommand command) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (command.type == VideoCommandType::kFrame) {
        // Coalesce: keep only the newest undrained frame.
        for (auto it = queue_.rbegin(); it != queue_.rend(); ++it) {
            if (it->type == VideoCommandType::kFrame) {
                *it = std::move(command);
                return;
            }
            // Stop scanning at the first non-frame boundary marker — a
            // kState/kError after the frame means the consumer should see
            // ordering, so only collapse frames that are adjacent-ish.
            // (We still scan the whole queue: stale frames anywhere are
            // safe to drop because only the newest matters.)
        }
        // No pending frame — fall through and append.
    }
    if (queue_.size() >= kMaxQueued) {
        // Drop the oldest non-frame command first; frames are cheap to
        // lose anyway, so evict from the front.
        queue_.pop_front();
    }
    queue_.push_back(std::move(command));
}

void VideoCommandBuffer::PushFrame(VideoFramePtr frame) {
    VideoCommand cmd;
    cmd.type = VideoCommandType::kFrame;
    cmd.frame = std::move(frame);
    Push(std::move(cmd));
}

void VideoCommandBuffer::PushState(VideoState state) {
    VideoCommand cmd;
    cmd.type = VideoCommandType::kState;
    cmd.state = state;
    Push(std::move(cmd));
}

void VideoCommandBuffer::PushError(std::string message) {
    VideoCommand cmd;
    cmd.type = VideoCommandType::kError;
    cmd.message = std::move(message);
    Push(std::move(cmd));
}

void VideoCommandBuffer::PushAudioReady() {
    VideoCommand cmd;
    cmd.type = VideoCommandType::kAudioReady;
    Push(std::move(cmd));
}

void VideoCommandBuffer::PushAudioError(std::string message) {
    VideoCommand cmd;
    cmd.type = VideoCommandType::kAudioError;
    cmd.message = std::move(message);
    Push(std::move(cmd));
}

std::size_t VideoCommandBuffer::Drain(std::vector<VideoCommand>* out) {
    if (out == nullptr) return 0;
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.empty()) return 0;
    out->clear();
    out->reserve(queue_.size());
    for (auto& cmd : queue_) {
        out->push_back(std::move(cmd));
    }
    queue_.clear();
    return out->size();
}

void VideoCommandBuffer::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.clear();
}

std::size_t VideoCommandBuffer::Pending() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
}

}  // namespace XER::Content::Video
