/**
 * WatchTimeRecorder — accumulates playback watch-time keyed by
 * properties (design doc: watch-time reporting powers metrics/UX).
 * Recording is bucketed: normal / muted / backgrounded.
 */
#pragma once

#include <map>
#include <string>

#include "media/base/media_types.h"

namespace XER::Media {

enum class WatchTimeKey : std::uint8_t {
    kAll,
    kMuted,
    kBackground,
    kMse,
    kEme,
};

class WatchTimeRecorder {
public:
    // |is_background|/|is_muted| select the bucket in addition to kAll.
    void RecordWatchTime(WatchTimeKey key, TimeTicks delta_us);
    TimeTicks watch_time(WatchTimeKey key) const;
    void Reset();

private:
    std::map<WatchTimeKey, TimeTicks> totals_;
};

}  // namespace XER::Media
