/**
 * AudioLatency — latency estimation utilities (design doc: the renderer
 * adds device latency to the time source so A/V stays synced).
 */
#pragma once

#include "media/audio/audio_parameters.h"
#include "media/base/media_types.h"

namespace XER::Media {

namespace audio_latency {

// Buffered-frames → microseconds at the params' sample rate.
inline TimeTicks BufferLatency(const AudioParameters& p) {
    if (p.sample_rate <= 0) return 0;
    return static_cast<TimeTicks>(p.frames_per_buffer) * 1000000 /
           p.sample_rate;
}

// Conservative platform estimate (buffer + ~5ms device pipeline).
inline TimeTicks EstimatedOutputLatency(const AudioParameters& p) {
    return BufferLatency(p) + 5000;
}

}  // namespace audio_latency

}  // namespace XER::Media
