#include "audio_bit_depth_converter.h"

#include <cstring>
#include <algorithm>
#include <cmath>

namespace XER::Engine::Audio::Core {

AudioBitDepthConverter::AudioBitDepthConverter() {}

AudioBitDepthConverter::~AudioBitDepthConverter() {}

bool AudioBitDepthConverter::ConvertBuffer(
    const void* input_buffer,
    AudioBitDepth input_depth,
    void* output_buffer,
    AudioBitDepth output_depth,
    size_t sample_count) {
    
    if (!input_buffer || !output_buffer || sample_count == 0) {
        return false;
    }

    if (input_depth == output_depth) {
        size_t byte_size = sample_count * GetBytesPerSample(input_depth);
        std::memcpy(output_buffer, input_buffer, byte_size);
        return true;
    }

    // Handle common conversions
    if (input_depth == AudioBitDepth::BITS_8 && output_depth == AudioBitDepth::BITS_16) {
        return Convert8To16(static_cast<const uint8_t*>(input_buffer),
                           static_cast<int16_t*>(output_buffer), sample_count);
    }
    
    if (input_depth == AudioBitDepth::BITS_16 && output_depth == AudioBitDepth::BITS_8) {
        return Convert16To8(static_cast<const int16_t*>(input_buffer),
                           static_cast<uint8_t*>(output_buffer), sample_count);
    }

    if (input_depth == AudioBitDepth::BITS_16 && output_depth == AudioBitDepth::FLOAT_32) {
        return Convert16ToFloat(static_cast<const int16_t*>(input_buffer),
                               static_cast<float*>(output_buffer), sample_count);
    }

    if (input_depth == AudioBitDepth::FLOAT_32 && output_depth == AudioBitDepth::BITS_16) {
        return ConvertFloatTo16(static_cast<const float*>(input_buffer),
                               static_cast<int16_t*>(output_buffer), sample_count);
    }

    return false;
}

bool AudioBitDepthConverter::ConvertMultiChannel(
    const std::vector<const void*>& input_buffers,
    AudioBitDepth input_depth,
    std::vector<void*>& output_buffers,
    AudioBitDepth output_depth,
    size_t sample_count,
    int channel_count) {
    
    if (input_buffers.size() != output_buffers.size() || 
        static_cast<int>(input_buffers.size()) != channel_count) {
        return false;
    }

    for (int ch = 0; ch < channel_count; ++ch) {
        if (!ConvertBuffer(input_buffers[ch], input_depth, output_buffers[ch], output_depth, sample_count)) {
            return false;
        }
    }

    return true;
}

size_t AudioBitDepthConverter::GetBytesPerSample(AudioBitDepth depth) {
    switch (depth) {
        case AudioBitDepth::BITS_8: return 1;
        case AudioBitDepth::BITS_16: return 2;
        case AudioBitDepth::BITS_24: return 3;
        case AudioBitDepth::BITS_32: return 4;
        case AudioBitDepth::FLOAT_32: return 4;
        default: return 0;
    }
}

bool AudioBitDepthConverter::IsFloatingPoint(AudioBitDepth depth) {
    return depth == AudioBitDepth::FLOAT_32;
}

bool AudioBitDepthConverter::IsSupported(AudioBitDepth depth) {
    return depth == AudioBitDepth::BITS_8 ||
           depth == AudioBitDepth::BITS_16 ||
           depth == AudioBitDepth::BITS_24 ||
           depth == AudioBitDepth::BITS_32 ||
           depth == AudioBitDepth::FLOAT_32;
}

bool AudioBitDepthConverter::NormalizeSamples(float* samples, size_t count, float gain) {
    if (!samples || count == 0) return false;
    
    for (size_t i = 0; i < count; ++i) {
        samples[i] = std::clamp(samples[i] * gain, -1.0f, 1.0f);
    }
    
    return true;
}

bool AudioBitDepthConverter::DenormalizeSamples(const float* samples, void* output, AudioBitDepth depth, size_t count) {
    if (!samples || !output || count == 0) return false;
    
    return ConvertBuffer(samples, AudioBitDepth::FLOAT_32, output, depth, count);
}

bool AudioBitDepthConverter::Convert8To16(const uint8_t* in, int16_t* out, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        // Convert unsigned 8-bit to signed 16-bit
        out[i] = static_cast<int16_t>((in[i] - 128) << 8);
    }
    return true;
}

bool AudioBitDepthConverter::Convert16To8(const int16_t* in, uint8_t* out, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        // Convert signed 16-bit to unsigned 8-bit
        out[i] = static_cast<uint8_t>((in[i] >> 8) + 128);
    }
    return true;
}

bool AudioBitDepthConverter::Convert16ToFloat(const int16_t* in, float* out, size_t count) {
    const float scale = 1.0f / 32768.0f;
    for (size_t i = 0; i < count; ++i) {
        out[i] = in[i] * scale;
    }
    return true;
}

bool AudioBitDepthConverter::ConvertFloatTo16(const float* in, int16_t* out, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        float clamped = std::clamp(in[i], -1.0f, 1.0f);
        out[i] = static_cast<int16_t>(clamped * 32767.0f);
    }
    return true;
}

}  // namespace XER::Engine::Audio::Core
