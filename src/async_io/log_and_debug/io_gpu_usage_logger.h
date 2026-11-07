#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <chrono>

namespace XER::AsyncIO::IO::LogDebug {

struct GPUUsageSnapshot {
    int device_id;
    double utilization_percent;
    double memory_used_mb;
    double memory_free_mb;
    double temperature_celsius;
    double power_draw_watts;
    int64_t timestamp_ms;
};

class GPUUsageLogger {
public:
    GPUUsageLogger();
    ~GPUUsageLogger();

    // Initialization
    bool Initialize();
    bool IsInitialized() const;

    // Logging control
    void StartLogging();
    void StopLogging();
    bool IsLogging() const { return is_logging_; }

    // Snapshot collection
    bool CaptureSnapshot(int device_id = 0);
    std::vector<GPUUsageSnapshot> GetSnapshots(int device_id = 0) const;

    // Statistics
    double GetAverageUtilization(int device_id = 0) const;
    double GetMaxUtilization(int device_id = 0) const;
    double GetAverageTemperature(int device_id = 0) const;
    double GetPeakMemoryUsage(int device_id = 0) const;

    // Reporting
    std::string GetUsageReport(int device_id = 0) const;
    void ClearSnapshots();

private:
    std::vector<GPUUsageSnapshot> snapshots_;
    bool initialized_;
    bool is_logging_;

    bool QueryGPUUsage(int device_id, GPUUsageSnapshot& snapshot);
};

}  // namespace XER::AsyncIO::IO::LogDebug
