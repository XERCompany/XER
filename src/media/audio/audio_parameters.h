/**
 * AudioParameters — stream format negotiated between clients and
 * platform streams: sample rate, channel layout, buffer size.
 */
#pragma once

#include <cstdint>

#include "media/base/channel_layout.h"
#include "media/base/sample_format.h"

namespace XER::Media {

struct AudioParameters {
    int sample_rate = 48000;
    ChannelLayout channel_layout = ChannelLayout::kStereo;
    SampleFormat format = SampleFormat::kF32;
    int frames_per_buffer = 480;  // 10ms at 48kHz

    int channels() const { return ChannelLayoutCount(channel_layout); }
    int bits_per_sample() const { return SampleFormatBytes(format) * 8; }
    int GetBytesPerBuffer() const {
        return frames_per_buffer * channels() * SampleFormatBytes(format);
    }
    bool IsValid() const {
        return sample_rate > 0 && channels() > 0 &&
               SampleFormatBytes(format) > 0 && frames_per_buffer > 0;
    }
};

}  // namespace XER::Media
