#include "media/muxers/muxer_timestamp_adapter.h"

namespace XER::Media {

TimeTicks MuxerTimestampAdapter::Normalize(StreamType stream_type,
                                           TimeTicks raw) {
    TimeTicks* origin = stream_type == StreamType::kAudio ? &audio_origin_
                                                        : &video_origin_;
    TimeTicks* last = stream_type == StreamType::kAudio ? &audio_last_
                                                      : &video_last_;
    if (raw == kNoTimestamp) return raw;
    if (*origin == kNoTimestamp) *origin = raw;
    TimeTicks t = raw - *origin;
    if (t < 0) t = 0;
    if (*last != kNoTimestamp && t < *last) t = *last;  // keep monotonic
    *last = t;
    return t;
}

void MuxerTimestampAdapter::Reset() {
    audio_origin_ = video_origin_ = audio_last_ = video_last_ = kNoTimestamp;
}

}  // namespace XER::Media
