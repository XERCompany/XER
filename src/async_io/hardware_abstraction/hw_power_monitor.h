#pragma once

#include <atomic>
#include <chrono>
#include <thread>
#include <mutex>

namespace AsyncIO::IO::Hardware {

struct PowerStats {
    double cpu_temp_celsius;        // Best-effort CPU temperature (0 if unsupported)
    double cpu_power_watts;        // CPU package power (RAPL on Linux; 0 on Windows)
    int    throttle_count;         // Linux thermal throttle events (0 if N/A)
    double gpu_power_watts;        // Sum of NVIDIA GPU power draw via NVML
    double gpu_power_limit_watts;  // Sum of NVIDIA GPU power limits via NVML
    bool   cpu_power_supported;    // RAPL/sysfs CPU power available (Linux only)
    bool   gpu_power_supported;    // NVML GPU power available
};

class HwPowerMonitor {
public:
    HwPowerMonitor();
    ~HwPowerMonitor();

    // Start a background thread refreshing stats every interval_ms (default 1000).
    void Start(int interval_ms = 1000);
    void Stop();

    PowerStats GetStats() const;
    bool IsRunning() const { return running_.load(std::memory_order_relaxed); }

private:
    void MonitorLoop(int interval_ms);
    void Refresh(PowerStats& out);

    std::atomic<bool> running_;
    std::thread monitor_thread_;
    mutable std::mutex stats_mutex_;
    PowerStats last_stats_;
};

}  // namespace AsyncIO::IO::Hardware
