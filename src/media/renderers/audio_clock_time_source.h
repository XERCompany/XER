/**
 * AudioClockTimeSource — TimeSource backed by AudioClock (design doc:
 * the audio renderer is the master clock; when no audio stream exists
 * callers use WallClockTimeSource). WroteAudio() is fed by the sink's
 * frame-consumption position.
 */
#pragma once

#include "media/base/audio_clock.h"
#include "media/renderers/time_source.h"

namespace XER::Media {

class AudioClockTimeSource final : public TimeSource {
public:
    explicit AudioClockTimeSource(int sample_rate) : clock_(sample_rate) {}

    TimeTicks CurrentMediaTime() const override { return clock_.Now(); }
    void SetPlaybackRate(double) override {}  // rate handled by resampler
    double playback_rate() const override { return 1.0; }

    // Sink plumbing.
    AudioClock* clock() { return &clock_; }
    void WroteAudio(int frames) { clock_.WroteAudio(frames); }
    void StartMediaTime(TimeTicks t) {
        clock_.SetBaseTime(t);
        clock_.Resume();
    }
    void StopClock() { clock_.Pause(); }

private:
    AudioClock clock_;
};

}  // namespace XER::Media
