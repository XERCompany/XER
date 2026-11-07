#pragma once

#include <cstddef>
#include <vector>
#include <memory>
#include <cstdint>
#include <string>

namespace XER::Engine::Audio::Core {

// Audio buffer layout
enum class AudioLayout {
    INTERLEAVED,
    PLANAR
};

struct AudioFrameInfo {
    int sample_rate;
    int channel_count;
    int bit_depth;
    AudioLayout layout;
};

class AudioBuffer {
public:
    AudioBuffer(const AudioFrameInfo& info, size_t frame_count);
    ~AudioBuffer();

    // Data access
    uint8_t* GetData() { return data_.data(); }
    const uint8_t* GetData() const { return data_.data(); }
    
    float* GetFloatData() { return reinterpret_cast<float*>(data_.data()); }
    const float* GetFloatData() const { return reinterpret_cast<const float*>(data_.data()); }

    // Information
    const AudioFrameInfo& GetInfo() const { return info_; }
    size_t GetFrameCount() const { return frame_count_; }
    size_t GetByteSize() const { return data_.size(); }
    size_t GetSampleCount() const { return frame_count_ * info_.channel_count; }

    // Operations
    void Clear();
    void Fill(float value);
    bool Resize(size_t new_frame_count);

private:
    AudioFrameInfo info_;
    size_t frame_count_;
    std::vector<uint8_t> data_;

    size_t CalculateByteSize() const;
};

class AudioBufferManager {
public:
    explicit AudioBufferManager(size_t initial_pool_size = 4);
    ~AudioBufferManager();

    // Buffer acquisition
    std::shared_ptr<AudioBuffer> AcquireBuffer(const AudioFrameInfo& info, size_t frame_count);
    void ReleaseBuffer(std::shared_ptr<AudioBuffer> buffer);

    // Pool management
    void Resize(size_t pool_size);
    void Clear();
    
    // Statistics
    size_t GetAvailableBuffers() const;
    size_t GetTotalBuffers() const;
    std::string GetPoolStatus() const;

private:
    std::vector<std::shared_ptr<AudioBuffer>> all_buffers_;
    std::vector<std::shared_ptr<AudioBuffer>> available_buffers_;
};

}  // namespace XER::Engine::Audio::Core
