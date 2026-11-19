/**
 * AudioBus — planar float audio container (design doc: decoded audio is
 * carried between stages as channel-planar f32; converts to/from
 * interleaved at the pipeline edges).
 */
#pragma once

#include <cstdint>
#include <vector>

#include "media/base/channel_layout.h"
#include "media/base/sample_format.h"

namespace XER::Media {

class AudioBus {
public:
    // Creates a bus of |channels| x |frames| zeroed f32 samples.
    static AudioBus Create(int channels, int frames);
    static AudioBus CreateForLayout(ChannelLayout layout, int frames);

    int channels() const { return channels_; }
    int frames() const { return frames_; }
    float* channel(int c) { return data_.data() + static_cast<size_t>(c) * frames_; }
    const float* channel(int c) const {
        return data_.data() + static_cast<size_t>(c) * frames_;
    }

    void Zero();
    // Interleaved f32 <-> planar conversion (frames <-> this->frames()).
    void FromInterleaved(const float* src, int frames);
    void ToInterleaved(int frames, float* dest) const;
    // Sample-format aware edge conversion.
    void FromInterleavedFormat(SampleFormat fmt, const std::uint8_t* src,
                               int frames);
    void ToInterleavedFormat(SampleFormat fmt, int frames,
                             std::uint8_t* dest) const;

private:
    int channels_ = 0;
    int frames_ = 0;
    std::vector<float> data_;  // channel-planar
};

}  // namespace XER::Media
