#pragma once

#include <cstddef>
#include <vector>
#include <cstdint>
#include <mutex>
#include <memory>

namespace XER::Engine::Audio::Core {

class AudioFIFOQueue {
public:
    explicit AudioFIFOQueue(size_t capacity);
    ~AudioFIFOQueue();

    // Write operations
    size_t Write(const float* data, size_t frame_count);
    size_t Write(const float* const* data, int channels, size_t frame_count);

    // Read operations
    size_t Read(float* data, size_t frame_count);
    size_t Read(float* const* data, int channels, size_t frame_count);

    // Status
    size_t GetAvailableFrames() const;
    size_t GetCapacity() const { return capacity_; }
    bool IsEmpty() const { return available_frames_ == 0; }
    bool IsFull() const { return available_frames_ >= capacity_; }

    // Control
    void Clear();
    void Reset();

    // Statistics
    uint64_t GetTotalWritten() const { return total_written_; }
    uint64_t GetTotalRead() const { return total_read_; }
    size_t GetOverflowCount() const { return overflow_count_; }

private:
    std::vector<float> buffer_;
    size_t capacity_;
    size_t read_pos_;
    size_t write_pos_;
    size_t available_frames_;
    mutable std::mutex mutex_;
    
    uint64_t total_written_;
    uint64_t total_read_;
    size_t overflow_count_;
};

}  // namespace XER::Engine::Audio::Core
