#pragma once

#include <cstddef>
#include <memory>
#include <vector>
#include <string>
#include <cstdint>
#include <functional>

namespace XER::Audio::Core {

// Processing callback
using AudioProcessCallback = std::function<void(float** inputs, float** outputs, int channels, size_t frames)>;

class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();

    // Initialization
    bool Initialize(int sample_rate, int buffer_size, int channel_count);
    bool IsInitialized() const { return initialized_; }

    // Process control
    bool Start();
    bool Stop();
    bool IsRunning() const { return is_running_; }

    // Configuration
    bool SetBufferSize(int frames);
    bool SetSampleRate(int sample_rate);
    int GetSampleRate() const { return sample_rate_; }
    int GetBufferSize() const { return buffer_size_; }
    int GetChannelCount() const { return channel_count_; }

    // Processing pipeline
    void SetProcessCallback(AudioProcessCallback callback);
    bool ProcessAudio(float** inputs, float** outputs, size_t frame_count);

    // Metering
    std::vector<float> GetInputLevels() const;
    std::vector<float> GetOutputLevels() const;
    double GetCPULoad() const { return cpu_load_; }

    // Information
    std::string GetEngineInfo() const;

private:
    bool initialized_;
    bool is_running_;
    int sample_rate_;
    int buffer_size_;
    int channel_count_;
    double cpu_load_;
    AudioProcessCallback process_callback_;

    std::vector<float> input_levels_;
    std::vector<float> output_levels_;

    void UpdateMeters(float** inputs, float** outputs, size_t frame_count);
};

}  // namespace XER::Audio::Core
