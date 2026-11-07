#include "audio_buffer_manager.h"

#include <algorithm>

namespace XER::Engine::Audio::Core {

AudioBuffer::AudioBuffer(const AudioFrameInfo& info, size_t frame_count)
    : info_(info), frame_count_(frame_count) {
    data_.resize(CalculateByteSize());
}

AudioBuffer::~AudioBuffer() {
    data_.clear();
}

void AudioBuffer::Clear() {
    std::fill(data_.begin(), data_.end(), 0);
}

void AudioBuffer::Fill(float value) {
    float* fdata = GetFloatData();
    for (size_t i = 0; i < GetSampleCount(); ++i) {
        fdata[i] = value;
    }
}

bool AudioBuffer::Resize(size_t new_frame_count) {
    frame_count_ = new_frame_count;
    data_.resize(CalculateByteSize());
    return true;
}

size_t AudioBuffer::CalculateByteSize() const {
    return frame_count_ * info_.channel_count * (info_.bit_depth / 8);
}

AudioBufferManager::AudioBufferManager(size_t initial_pool_size) {
    for (size_t i = 0; i < initial_pool_size; ++i) {
        AudioFrameInfo default_info{44100, 2, 16, AudioLayout::INTERLEAVED};
        auto buffer = std::make_shared<AudioBuffer>(default_info, 1024);
        all_buffers_.push_back(buffer);
        available_buffers_.push_back(buffer);
    }
}

AudioBufferManager::~AudioBufferManager() {
    Clear();
}

std::shared_ptr<AudioBuffer> AudioBufferManager::AcquireBuffer(const AudioFrameInfo& info, size_t frame_count) {
    std::shared_ptr<AudioBuffer> buffer;
    
    if (!available_buffers_.empty()) {
        buffer = available_buffers_.back();
        available_buffers_.pop_back();
        
        if (buffer->GetFrameCount() != frame_count) {
            buffer->Resize(frame_count);
        }
        buffer->Clear();
    } else {
        buffer = std::make_shared<AudioBuffer>(info, frame_count);
        all_buffers_.push_back(buffer);
    }
    
    return buffer;
}

void AudioBufferManager::ReleaseBuffer(std::shared_ptr<AudioBuffer> buffer) {
    if (buffer) {
        available_buffers_.push_back(buffer);
    }
}

void AudioBufferManager::Resize(size_t pool_size) {
    while (all_buffers_.size() < pool_size) {
        AudioFrameInfo default_info{44100, 2, 16, AudioLayout::INTERLEAVED};
        auto buffer = std::make_shared<AudioBuffer>(default_info, 1024);
        all_buffers_.push_back(buffer);
        available_buffers_.push_back(buffer);
    }
    
    while (all_buffers_.size() > pool_size) {
        all_buffers_.pop_back();
    }
}

void AudioBufferManager::Clear() {
    all_buffers_.clear();
    available_buffers_.clear();
}

size_t AudioBufferManager::GetAvailableBuffers() const {
    return available_buffers_.size();
}

size_t AudioBufferManager::GetTotalBuffers() const {
    return all_buffers_.size();
}

std::string AudioBufferManager::GetPoolStatus() const {
    std::string status = "Pool: " + std::to_string(available_buffers_.size()) + 
                        "/" + std::to_string(all_buffers_.size()) + " available";
    return status;
}

}  // namespace XER::Engine::Audio::Core
