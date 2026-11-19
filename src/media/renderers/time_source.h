/**
 * TimeSource — shared media clock for A/V sync (design doc). The audio
 * renderer typically owns the master clock; the video renderer queries it
 * to pick the due frame.
 */
#pragma once

#include "media/base/media_types.h"

namespace XER::Media {

class TimeSource {
public:
    virtual ~TimeSource() = default;
    virtual TimeTicks CurrentMediaTime() const = 0;
    virtual void SetPlaybackRate(double rate) = 0;
    virtual double playback_rate() const = 0;
};

// Monotonic wall-clock time source: media time advances at the wall rate
// multiplied by playback_rate. StartMediaTime() sets the media-time
// origin (e.g. after seek); StopClock() pauses accumulation.
class WallClockTimeSource final : public TimeSource {
public:
    WallClockTimeSource();

    TimeTicks CurrentMediaTime() const override;
    void SetPlaybackRate(double rate) override;
    double playback_rate() const override { return rate_; }

    void StartMediaTime(TimeTicks media_time);
    void StopClock();

private:
    double rate_ = 1.0;
    TimeTicks media_origin_ = 0;
    std::int64_t wall_origin_us_ = 0;  // steady clock at Start
    bool running_ = false;
};

}  // namespace XER::Media
