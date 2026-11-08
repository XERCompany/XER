#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <chrono>

#include "async_io/log_and_debug/io_perf_counter.h"

namespace XER::Audio::Core {

struct LatencyInfo {
    double input_latency_ms;
    double output_latency_ms;
    double total_round_trip_ms;
    double processing_latency_ms;
};

class LatencyMonitor {
public:
    LatencyMonitor();
    ~LatencyMonitor();

    // Monitoring control
    bool StartMonitoring();
    bool StopMonitoring();
    bool IsMonitoring() const { return is_monitoring_; }

    // Latency measurement
    void RecordInputTimestamp();
    void RecordOutputTimestamp();
    LatencyInfo CalculateLatency() const;

    // Statistics
    std::vector<double> GetLatencyHistory() const;
    double GetAverageLatency() const;
    double GetMaxLatency() const;
    double GetMinLatency() const;

    // Performance integration
    const XER::AsyncIO::IO::LogDebug::PerformanceCounter& GetPerformanceCounter() const { return perf_counter_; }

    // Report
    std::string GetLatencyReport() const;

private:
    bool is_monitoring_;
    std::chrono::steady_clock::time_point input_timestamp_;
    std::chrono::steady_clock::time_point output_timestamp_;
    std::vector<double> latency_history_;
    XER::AsyncIO::IO::LogDebug::PerformanceCounter perf_counter_;

    void UpdateHistory(double latency_ms);
};

}  // namespace XER::Audio::Core
