#include "audio_fifo_queue.h"

#include <cstring>
#include <algorithm>
#include <limits>

namespace Engine::Audio::Core {

AudioFIFOQueue::AudioFIFOQueue(size_t capacity)
    : capacity_(capacity), read_pos_(0), write_pos_(0), available_frames_(0),
      total_written_(0), total_read_(0), overflow_count_(0) {
    buffer_.resize(capacity);
}

AudioFIFOQueue::~AudioFIFOQueue() {}

size_t AudioFIFOQueue::Write(const float* data, size_t frame_count) {
    if (!data || frame_count == 0 || capacity_ == 0) return 0;

    std::lock_guard<std::mutex> lock(mutex_);
    
    size_t writable = capacity_ - available_frames_;
    size_t to_write = std::min(frame_count, writable);
    
    if (to_write < frame_count) {
        overflow_count_++;
    }

    // Copy to circular buffer
    size_t write_now = std::min(to_write, capacity_ - write_pos_);
    std::memcpy(buffer_.data() + write_pos_, data, write_now * sizeof(float));
    
    if (write_now < to_write) {
        std::memcpy(buffer_.data(), data + write_now, (to_write - write_now) * sizeof(float));
    }

    write_pos_ = (write_pos_ + to_write) % capacity_;
    available_frames_ += to_write;
    total_written_ += to_write;
    
    return to_write;
}

size_t AudioFIFOQueue::Write(const float* const* data, int channels, size_t frame_count) {
    if (!data || channels <= 0 || frame_count == 0) return 0;
    if (capacity_ == 0 || frame_count > std::numeric_limits<size_t>::max() / static_cast<size_t>(channels)) return 0;
    for (int channel = 0; channel < channels; ++channel) {
        if (data[channel] == nullptr) return 0;
    }

    const size_t sample_count = frame_count * static_cast<size_t>(channels);
    std::lock_guard<std::mutex> lock(mutex_);
    const size_t writable = capacity_ - available_frames_;
    const size_t to_write = std::min(sample_count, writable);
    if (to_write < sample_count) ++overflow_count_;

    const size_t write_now = std::min(to_write, capacity_ - write_pos_);
    for (size_t sample_index = 0; sample_index < to_write; ++sample_index) {
        const size_t source_index = sample_index;
        const size_t channel = source_index % static_cast<size_t>(channels);
        const size_t frame = source_index / static_cast<size_t>(channels);
        const size_t destination_index = sample_index < write_now
            ? write_pos_ + sample_index
            : sample_index - write_now;
        buffer_[destination_index] = data[channel][frame];
    }

    write_pos_ = (write_pos_ + to_write) % capacity_;
    available_frames_ += to_write;
    total_written_ += to_write;
    return to_write;
}

size_t AudioFIFOQueue::Read(float* data, size_t frame_count) {
    if (!data || frame_count == 0 || capacity_ == 0) return 0;

    std::lock_guard<std::mutex> lock(mutex_);
    
    size_t to_read = std::min(frame_count, available_frames_);

    // Copy from circular buffer
    size_t read_now = std::min(to_read, capacity_ - read_pos_);
    std::memcpy(data, buffer_.data() + read_pos_, read_now * sizeof(float));
    
    if (read_now < to_read) {
        std::memcpy(data + read_now, buffer_.data(), (to_read - read_now) * sizeof(float));
    }

    read_pos_ = (read_pos_ + to_read) % capacity_;
    available_frames_ -= to_read;
    total_read_ += to_read;
    
    return to_read;
}

size_t AudioFIFOQueue::Read(float* const* data, int channels, size_t frame_count) {
    if (!data || channels <= 0 || frame_count == 0) return 0;
    if (capacity_ == 0 || frame_count > std::numeric_limits<size_t>::max() / static_cast<size_t>(channels)) return 0;
    for (int channel = 0; channel < channels; ++channel) {
        if (data[channel] == nullptr) return 0;
    }

    const size_t sample_count = frame_count * static_cast<size_t>(channels);
    std::lock_guard<std::mutex> lock(mutex_);
    const size_t to_read = std::min(sample_count, available_frames_);
    const size_t frames_read = to_read / static_cast<size_t>(channels);
    const size_t complete_samples = frames_read * static_cast<size_t>(channels);
    const size_t read_now = std::min(to_read, capacity_ - read_pos_);

    for (size_t sample_index = 0; sample_index < complete_samples; ++sample_index) {
        const size_t channel = sample_index % static_cast<size_t>(channels);
        const size_t frame = sample_index / static_cast<size_t>(channels);
        const size_t source_index = sample_index < read_now
            ? read_pos_ + sample_index
            : sample_index - read_now;
        data[channel][frame] = buffer_[source_index];
    }

    read_pos_ = (read_pos_ + to_read) % capacity_;
    available_frames_ -= to_read;
    total_read_ += to_read;
    return frames_read;
}

size_t AudioFIFOQueue::GetAvailableFrames() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return available_frames_;
}

void AudioFIFOQueue::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::fill(buffer_.begin(), buffer_.end(), 0.0f);
    read_pos_ = 0;
    write_pos_ = 0;
    available_frames_ = 0;
}

void AudioFIFOQueue::Reset() {
    Clear();
    total_written_ = 0;
    total_read_ = 0;
    overflow_count_ = 0;
}

}  // namespace Engine::Audio::Core
