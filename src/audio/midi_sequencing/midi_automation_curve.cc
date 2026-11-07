#include "midi_automation_curve.h"

#include <algorithm>

namespace XER::Engine::Audio::MIDI {

void MidiAutomationCurve::AddPoint(uint32_t tick, float value) {
    XER::AsyncIO::IO::Sync::MutexWrapper::ScopedLock lock(mutex_);
    points_.push_back(AutomationPoint{tick, value});
    std::sort(points_.begin(), points_.end(), [](const AutomationPoint& a, const AutomationPoint& b) {
        return a.tick < b.tick;
    });
}

float MidiAutomationCurve::Evaluate(uint32_t tick) const {
    XER::AsyncIO::IO::Sync::MutexWrapper::ScopedLock lock(mutex_);
    if (points_.empty()) {
        return 0.0f;
    }
    if (tick <= points_.front().tick) {
        return points_.front().value;
    }
    if (tick >= points_.back().tick) {
        return points_.back().value;
    }

    for (size_t i = 1; i < points_.size(); ++i) {
        if (tick <= points_[i].tick) {
            const auto& p0 = points_[i - 1];
            const auto& p1 = points_[i];
            const float t = static_cast<float>(tick - p0.tick) / static_cast<float>(p1.tick - p0.tick);
            return p0.value + (p1.value - p0.value) * t;
        }
    }

    return points_.back().value;
}

}  // namespace XER::Engine::Audio::MIDI
