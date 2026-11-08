#pragma once

#include <cstddef>
#include <vector>
#include <cstdint>

namespace XER::Audio::Core {

class AudioInterleaveProcessor {
public:
    AudioInterleaveProcessor();
    ~AudioInterleaveProcessor();

    // Interleaving
    static bool Interleave(
        const float* const* planar_data,
        int channel_count,
        size_t frame_count,
        float* interleaved_output
    );

    // Deinterleaving
    static bool Deinterleave(
        const float* interleaved_data,
        int channel_count,
        size_t frame_count,
        float* const* planar_output
    );

    // In-place operations
    static bool InterleavePlanar(
        std::vector<std::vector<float>>& channels,
        size_t frame_count
    );

    static bool DeinterleavePlanar(
        const float* interleaved,
        int channel_count,
        size_t frame_count,
        std::vector<std::vector<float>>& output_channels
    );

    // Channel manipulation
    static bool DuplicateChannel(
        const float* source,
        float* dest,
        size_t frame_count
    );

    static bool MixChannels(
        const float* const* inputs,
        int input_channels,
        float* output,
        size_t frame_count
    );

private:
    // Helpers returning calculation sizes
    static size_t CalculateInterleaveSize(int channels, size_t frames) {
        return channels * frames;
    }
};

}  // namespace XER::Audio::Core
