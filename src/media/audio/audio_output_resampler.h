/**
 * AudioOutputResampler — format/rate conversion between clients and
 * physical output streams (design doc: sits between clients and
 * AudioOutputStream, handles conversion and stream reuse).
 */
#pragma once

#include <cstdint>
#include <vector>

#include "media/audio/audio_parameters.h"

namespace XER::Media {

class AudioOutputResampler {
public:
    AudioOutputResampler(const AudioParameters& from,
                         const AudioParameters& to);

    bool NeedsResample() const {
        return from_.sample_rate != to_.sample_rate ||
               from_.format != to_.format ||
               from_.channel_layout != to_.channel_layout;
    }

    // Converts |frames_in| interleaved input frames; appends to |out|
    // (target format, interleaved). Linear interpolation — adequate for
    // the sink path; production uses the DSP resampler when linked.
    int Resample(const std::uint8_t* src, int frames_in,
                 std::vector<std::uint8_t>* out);

private:
    AudioParameters from_;
    AudioParameters to_;
    double phase_ = 0.0;
};

}  // namespace XER::Media
