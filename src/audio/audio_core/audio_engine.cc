#include "audio_engine.h"
#include "audio/audio_core/audio_logger.h"

#include <algorithm>
#include <cmath>

namespace XER::Engine::Audio::Core {

AudioEngine::AudioEngine()
    : initialized_(false), is_running_(false), sample_rate_(44100),
      buffer_size_(512), channel_count_(2), cpu_load_(0.0) {}

AudioEngine::~AudioEngine() {
    if (is_running_) {
        Stop();
    }
}

bool AudioEngine::Initialize(int sample_rate, int buffer_size, int channel_count) {
    sample_rate_ = sample_rate;
    buffer_size_ = buffer_size;
    channel_count_ = channel_count;

    input_levels_.resize(channel_count, 0.0f);
    output_levels_.resize(channel_count, 0.0f);

    initialized_ = true;
    AudioLogger::Instance().LogEngineStart(sample_rate, buffer_size);
    return true;
}

bool AudioEngine::Start() {
    if (!initialized_) return false;

    is_running_ = true;
    AudioLogger::Instance().LogEngineStart(sample_rate_, buffer_size_);
    return true;
}

bool AudioEngine::Stop() {
    is_running_ = false;
    AudioLogger::Instance().LogEngineStop();
    return true;
}

bool AudioEngine::SetBufferSize(int frames) {
    buffer_size_ = frames;
    return true;
}

bool AudioEngine::SetSampleRate(int sample_rate) {
    sample_rate_ = sample_rate;
    return true;
}

void AudioEngine::SetProcessCallback(AudioProcessCallback callback) {
    process_callback_ = callback;
}

bool AudioEngine::ProcessAudio(float** inputs, float** outputs, size_t frame_count) {
    if (!is_running_) return false;

    UpdateMeters(inputs, outputs, frame_count);
    
    if (process_callback_) {
        process_callback_(inputs, outputs, channel_count_, frame_count);
    }
    
    return true;
}

std::vector<float> AudioEngine::GetInputLevels() const {
    return input_levels_;
}

std::vector<float> AudioEngine::GetOutputLevels() const {
    return output_levels_;
}

std::string AudioEngine::GetEngineInfo() const {
    std::string info = "AudioEngine: ";
    info += std::to_string(sample_rate_) + "Hz, ";
    info += std::to_string(buffer_size_) + " frames, ";
    info += std::to_string(channel_count_) + " channels";
    return info;
}

void AudioEngine::UpdateMeters(float** inputs, float** outputs, size_t frame_count) {
    // Calculate RMS levels for each channel
    for (int ch = 0; ch < channel_count_; ++ch) {
        if (inputs && inputs[ch]) {
            float sum_sq = 0.0f;
            for (size_t i = 0; i < frame_count; ++i) {
                sum_sq += inputs[ch][i] * inputs[ch][i];
            }
            input_levels_[ch] = std::sqrt(sum_sq / frame_count);
        }
        
        if (outputs && outputs[ch]) {
            float sum_sq = 0.0f;
            for (size_t i = 0; i < frame_count; ++i) {
                sum_sq += outputs[ch][i] * outputs[ch][i];
            }
            output_levels_[ch] = std::sqrt(sum_sq / frame_count);
        }
    }
}

}  // namespace XER::Engine::Audio::Core
