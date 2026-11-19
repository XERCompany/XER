/**
 * AudioClock — media time from the audio output position (design doc:
 * the master clock is audio-derived; the video renderer pulls media
 * time through TimeSource which reads this clock).
 */
#pragma once

#include <atomic>

#include "media/base/media_types.h"

namespace XER::Media {

class AudioClock {
public:
    explicit AudioClock(int sample_rate = 48000)
        : sample_rate_(sample_rate > 0 ? sample_rate : 48000) {}

    // Called by the sink each time |frames| are consumed by the device.
    void WroteAudio(int frames);
    // Current media time in microseconds.
    TimeTicks Now() const;
    void SetBaseTime(TimeTicks t) { base_us_ = t; }
    void Reset(int frames_written = 0);
    void Pause();
    void Resume();
    bool paused() const { return paused_; }

private:
    int sample_rate_;
    std::atomic<std::int64_t> frames_written_{0};
    TimeTicks base_us_ = 0;
    TimeTicks paused_at_ = 0;
    bool paused_ = false;
};

}  // namespace XER::Media
