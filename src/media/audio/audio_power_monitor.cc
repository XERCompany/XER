#include "media/audio/audio_power_monitor.h"

#include <cmath>

namespace XER::Media {

double AudioPowerMonitor::Feed(const AudioBus& bus, int frames) {
    double sum = 0.0;
    int count = 0;
    for (int c = 0; c < bus.channels(); ++c) {
        const float* s = bus.channel(c);
        for (int f = 0; f < frames && f < bus.frames(); ++f) {
            sum += static_cast<double>(s[f]) * s[f];
            ++count;
        }
    }
    if (count == 0) return avg_dbfs_;
    const double rms = std::sqrt(sum / count);
    const double dbfs = rms > 1e-9 ? 20.0 * std::log10(rms) : -100.0;
    // Smooth over ~10 blocks.
    avg_dbfs_ = avg_dbfs_ * 0.9 + dbfs * 0.1;
    return avg_dbfs_;
}

}  // namespace XER::Media
