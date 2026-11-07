#pragma once

#include <string>
#include <cstdint>
#include <chrono>

namespace XER::AsyncIO::IO::Hardware {

struct MemoryStats {
    uint64_t total_mb;
    uint64_t used_mb;
    uint64_t free_mb;
    double usage_percent;
    uint64_t peak_usage_mb;
};

class MemoryMonitor {
public:
    MemoryMonitor();
    ~MemoryMonitor();

    // Monitoring
    bool StartMonitoring();
    bool StopMonitoring();
    bool IsMonitoring() const { return is_monitoring_; }

    // Statistics
    MemoryStats GetMemoryStats() const;
    MemoryStats GetGPUMemoryStats(int device_id) const;

    // Peak tracking
    uint64_t GetPeakMemoryUsage() const { return peak_memory_mb_; }
    void ResetPeakMemory();

    // Warnings
    void SetLowMemoryThreshold(double percent);
    bool IsLowMemory() const;

    // Allocation tracking
    void RecordAllocation(size_t bytes);
    void RecordDeallocation(size_t bytes);
    uint64_t GetTrackedAllocationMB() const;

private:
    bool is_monitoring_;
    uint64_t peak_memory_mb_;
    uint64_t tracked_allocation_mb_;
    double low_memory_threshold_;
    std::chrono::steady_clock::time_point start_time_;

    MemoryStats QuerySystemMemory() const;
};

}  // namespace XER::AsyncIO::IO::Hardware
