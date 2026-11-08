#pragma once

#include <cstddef>
#include <vector>
#include <cstdint>
#include <string>

#include "async_io/async_buffer_pool.h"

namespace XER::Audio::Core {

enum class ResampleQuality {
    FAST,
    MEDIUM,
    HIGH,
    BEST
};

class AudioSampleRateConverter {
public:
    explicit AudioSampleRateConverter(ResampleQuality quality = ResampleQuality::HIGH);
    ~AudioSampleRateConverter();

    // Initialization
    bool Initialize(int input_rate, int output_rate);
    bool IsInitialized() const { return initialized_; }

    // Conversion
    bool Convert(const float* input, size_t input_frames, float* output, size_t& output_frames);
    bool ConvertMultiChannel(
        const float* const* input,
        int channels,
        size_t input_frames,
        float* const* output,
        size_t& output_frames
    );

    // Information
    double GetRatio() const { return ratio_; }
    int GetInputRate() const { return input_rate_; }
    int GetOutputRate() const { return output_rate_; }
    ResampleQuality GetQuality() const { return quality_; }

    // State management
    void Reset() { buffer_pos_ = 0; }
    std::string GetInfo() const;

private:
    int input_rate_;
    int output_rate_;
    double ratio_;
    ResampleQuality quality_;
    bool initialized_;
    
    std::vector<float> filter_kernel_;
    double buffer_pos_;

    XER::AsyncIO::IO::Async::AsyncBufferPool buffer_pool_;

    void GenerateFilterKernel();
    size_t GetKernelRadius() const;
    float InterpolateLinear(const float* buffer, double pos) const;
    float InterpolateWindowedSinc(const float* buffer, size_t frame_count, double pos) const;
};

}  // namespace XER::Audio::Core
