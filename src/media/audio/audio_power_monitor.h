/**
 * AudioPowerMonitor — measures average output power for ducking
 * decisions (design doc: focus/duck logic watches whether the output is
 * actually producing audible signal).
 */
#pragma once

#include <cstdint>

#include "media/base/audio_bus.h"

namespace XER::Media {

class AudioPowerMonitor {
public:
    explicit AudioPowerMonitor(int sample_rate)
        : sample_rate_(sample_rate) {}

    // Feeds a block; returns current average power (dBFS approx).
    double Feed(const AudioBus& bus, int frames);
    double average_power_dbfs() const { return avg_dbfs_; }
    bool IsSilent(double threshold_dbfs = -60.0) const {
        return avg_dbfs_ < threshold_dbfs;
    }

private:
    int sample_rate_;
    double avg_dbfs_ = -100.0;
};

}  // namespace XER::Media
