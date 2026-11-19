#include "media/metrics/watch_time_recorder.h"

namespace XER::Media {

void WatchTimeRecorder::RecordWatchTime(WatchTimeKey key,
                                        TimeTicks delta_us) {
    if (delta_us <= 0) return;
    totals_[key] += delta_us;
}

TimeTicks WatchTimeRecorder::watch_time(WatchTimeKey key) const {
    const auto it = totals_.find(key);
    return it == totals_.end() ? 0 : it->second;
}

void WatchTimeRecorder::Reset() { totals_.clear(); }

}  // namespace XER::Media
