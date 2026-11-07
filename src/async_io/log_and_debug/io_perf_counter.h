#pragma once

#include <string>
#include <map>
#include <cstdint>
#include <chrono>
#include <mutex>
#include <vector>

namespace XER::AsyncIO::IO::LogDebug {

struct PerformanceMetric {
    std::string name;
    uint64_t count;
    double total_ms;
    double min_ms;
    double max_ms;
    double avg_ms;
    int64_t last_measured_ms;
};

class PerformanceCounter {
public:
    PerformanceCounter();
    ~PerformanceCounter();

    // Counter management
    void StartCounter(const std::string& name);
    void StopCounter(const std::string& name);
    void ResetCounter(const std::string& name);
    void ResetAll();

    // Metrics retrieval
    PerformanceMetric GetMetric(const std::string& name) const;
    std::vector<PerformanceMetric> GetAllMetrics() const;
    
    // Timing helpers
    double GetTotalTimeMS(const std::string& name) const;
    double GetAverageTimeMS(const std::string& name) const;
    uint64_t GetCallCount(const std::string& name) const;

    // Record a pre-measured elapsed interval directly. Used by ScopedTimer
    // and any caller that already has a measured duration. Equivalent to
    // StartCounter/StopCounter but without the active-counter bookkeeping.
    void RecordElapsed(const std::string& name, double elapsed_ms);

    // Reports
    std::string GetReport() const;
    std::string GetMetricReport(const std::string& name) const;

    // Enable/disable
    void Enable() { enabled_ = true; }
    void Disable() { enabled_ = false; }
    bool IsEnabled() const { return enabled_; }

private:
    mutable std::mutex mutex_;
    std::map<std::string, PerformanceMetric> metrics_;
    std::map<std::string, std::chrono::steady_clock::time_point> active_counters_;
    bool enabled_;

    void UpdateMetric(const std::string& name, double elapsed_ms);
};

// RAII scope timer. Records the elapsed time from construction to destruction
// into the given PerformanceCounter under the given name. Drop early to stop.
// Unlike StartCounter/StopCounter, this is exception-safe: the destructor
// always runs, so a thrown task still records its elapsed time.
//
// Usage:
//   {
//     ScopedTimer t(counter, "encode_frame");
//     EncodeFrame(...);
//   }  // ~ScopedTimer records the elapsed ms into counter.
class ScopedTimer {
public:
    ScopedTimer(PerformanceCounter& counter, const std::string& name);
    ~ScopedTimer();
    ScopedTimer(const ScopedTimer&) = delete;
    ScopedTimer& operator=(const ScopedTimer&) = delete;

    // Stop early and record now (subsequent destruction is a no-op).
    void Stop();

private:
    PerformanceCounter* counter_;
    std::string name_;
    std::chrono::steady_clock::time_point start_;
    bool stopped_;
};

}  // namespace XER::AsyncIO::IO::LogDebug
