#pragma once

#include <cstdint>
#include <chrono>
#include <vector>

namespace XER::Engine::Audio::Core {

// Clock source types
enum class ClockSource {
    INTERNAL,
    EXTERNAL,
    NETWORK
};

struct ClockInfo {
    ClockSource source;
    int sample_rate;
    int64_t frame_position;
    int64_t timestamp_ns;
    bool is_locked;
};

class AudioClockSync {
public:
    AudioClockSync();
    ~AudioClockSync();

    // Initialization
    bool Initialize(int sample_rate, ClockSource source = ClockSource::INTERNAL);
    bool IsInitialized() const { return initialized_; }

    // Clock operations
    void UpdateClock(int64_t frame_position);
    bool SyncToExternal(int64_t external_position, int64_t external_timestamp_ns);

    // Information
    const ClockInfo& GetClockInfo() const { return clock_info_; }
    int64_t GetCurrentFrame() const { return clock_info_.frame_position; }
    double GetCurrentTime() const;

    // Drift detection
    double GetDriftPercentage() const { return drift_percentage_; }
    bool CheckClockLock() const;

    // Sample rate conversion
    bool CalculateSampleRateRatio(int source_rate, int target_rate, double& ratio);

private:
    ClockInfo clock_info_;
    bool initialized_;
    double drift_percentage_;
    std::vector<int64_t> frame_history_;
    
    void UpdateDrift();
};

}  // namespace XER::Engine::Audio::Core
