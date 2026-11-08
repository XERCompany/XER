#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>
#include <memory>

namespace XER::Audio::Core {

enum class AudioBitDepth {
    BITS_8,
    BITS_16,
    BITS_24,
    BITS_32,
    FLOAT_32
};

class AudioBitDepthConverter {
public:
    AudioBitDepthConverter();
    ~AudioBitDepthConverter();

    // Conversion
    bool ConvertBuffer(
        const void* input_buffer,
        AudioBitDepth input_depth,
        void* output_buffer,
        AudioBitDepth output_depth,
        size_t sample_count
    );

    // Batch conversion
    bool ConvertMultiChannel(
        const std::vector<const void*>& input_buffers,
        AudioBitDepth input_depth,
        std::vector<void*>& output_buffers,
        AudioBitDepth output_depth,
        size_t sample_count,
        int channel_count
    );

    // Utilities
    static size_t GetBytesPerSample(AudioBitDepth depth);
    static bool IsFloatingPoint(AudioBitDepth depth);
    static bool IsSupported(AudioBitDepth depth);

    // Normalization
    bool NormalizeSamples(float* samples, size_t count, float gain = 1.0f);
    bool DenormalizeSamples(const float* samples, void* output, AudioBitDepth depth, size_t count);

private:
    std::vector<uint8_t> temp_buffer_;

    bool Convert8To16(const uint8_t* in, int16_t* out, size_t count);
    bool Convert16To8(const int16_t* in, uint8_t* out, size_t count);
    bool Convert16ToFloat(const int16_t* in, float* out, size_t count);
    bool ConvertFloatTo16(const float* in, int16_t* out, size_t count);
};

}  // namespace XER::Audio::Core
