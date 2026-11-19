/**
 * MuxerTimestampAdapter — aligns timestamps for streams with different
 * clocks (design doc). The first timestamp of each stream becomes that
 * stream's zero point; output stays monotonic per stream.
 */
#pragma once

#include "media/base/media_types.h"

namespace XER::Media {

class MuxerTimestampAdapter {
public:
    // Returns the normalised timestamp for |raw| coming from |stream_type|.
    TimeTicks Normalize(StreamType stream_type, TimeTicks raw);
    void Reset();

private:
    TimeTicks audio_origin_ = kNoTimestamp;
    TimeTicks video_origin_ = kNoTimestamp;
    TimeTicks audio_last_ = kNoTimestamp;
    TimeTicks video_last_ = kNoTimestamp;
};

}  // namespace XER::Media
