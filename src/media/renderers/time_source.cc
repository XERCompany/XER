#include "media/renderers/time_source.h"

#include <chrono>

namespace XER::Media {
namespace {
std::int64_t NowUs() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
}  // namespace

WallClockTimeSource::WallClockTimeSource() = default;

TimeTicks WallClockTimeSource::CurrentMediaTime() const {
    if (!running_) return media_origin_;
    const std::int64_t elapsed = NowUs() - wall_origin_us_;
    return media_origin_ + static_cast<TimeTicks>(elapsed * rate_);
}

void WallClockTimeSource::SetPlaybackRate(double rate) {
    if (rate <= 0.0) return;
    // Fold accumulated time into the origin at the new rate.
    media_origin_ = CurrentMediaTime();
    wall_origin_us_ = NowUs();
    rate_ = rate;
}

void WallClockTimeSource::StartMediaTime(TimeTicks media_time) {
    media_origin_ = media_time;
    wall_origin_us_ = NowUs();
    running_ = true;
}

void WallClockTimeSource::StopClock() {
    media_origin_ = CurrentMediaTime();
    running_ = false;
}

}  // namespace XER::Media
