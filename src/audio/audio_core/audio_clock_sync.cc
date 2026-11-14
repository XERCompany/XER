#include "audio_clock_sync.h"

#include <algorithm>
#include <numeric>

namespace XER::Audio::Core {

AudioClockSync::AudioClockSync()
    : initialized_(false), drift_percentage_(0.0) {
    clock_info_ = ClockInfo{ClockSource::INTERNAL, 0, 0, 0, false};
}

AudioClockSync::~AudioClockSync() {}

bool AudioClockSync::Initialize(int sample_rate, ClockSource source) {
    clock_info_.sample_rate = sample_rate;
    clock_info_.source = source;
    clock_info_.frame_position = 0;
    clock_info_.timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    clock_info_.is_locked = true;
    
    initialized_ = true;
    return true;
}

void AudioClockSync::UpdateClock(int64_t frame_position) {
    if (!initialized_) return;
    
    clock_info_.frame_position = frame_position;
    clock_info_.timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    
    frame_history_.push_back(frame_position);
    if (frame_history_.size() > 100) {
        frame_history_.erase(frame_history_.begin());
    }
    
    UpdateDrift();
}

bool AudioClockSync::SyncToExternal(int64_t external_position, int64_t external_timestamp_ns) {
    (void)external_timestamp_ns;
    if (!initialized_) return false;

    int64_t drift = external_position - clock_info_.frame_position;
    
    // Apply correction if drift exceeds threshold
    if (std::abs(drift) > clock_info_.sample_rate / 10) {
        clock_info_.frame_position = external_position;
        clock_info_.is_locked = true;
    }
    
    return clock_info_.is_locked;
}

double AudioClockSync::GetCurrentTime() const {
    if (clock_info_.sample_rate <= 0) return 0.0;
    return static_cast<double>(clock_info_.frame_position) / clock_info_.sample_rate;
}

bool AudioClockSync::CheckClockLock() const {
    return clock_info_.is_locked && initialized_;
}

bool AudioClockSync::CalculateSampleRateRatio(int source_rate, int target_rate, double& ratio) {
    if (source_rate <= 0 || target_rate <= 0) {
        return false;
    }
    
    ratio = static_cast<double>(target_rate) / static_cast<double>(source_rate);
    return true;
}

void AudioClockSync::UpdateDrift() {
    if (frame_history_.size() < 2) return;

    std::vector<int64_t> diffs;
    for (size_t i = 1; i < frame_history_.size(); ++i) {
        diffs.push_back(frame_history_[i] - frame_history_[i-1]);
    }

    double avg_diff = std::accumulate(diffs.begin(), diffs.end(), 0.0) / diffs.size();
    double expected_diff = 1.0;  // Expect 1 frame increment per update
    
    drift_percentage_ = (avg_diff - expected_diff) / expected_diff * 100.0;
}

}  // namespace XER::Audio::Core
