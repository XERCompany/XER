#include "io_perf_counter.h"

namespace AsyncIO::IO::LogDebug {

PerformanceCounter::PerformanceCounter()
    : enabled_(true) {}

PerformanceCounter::~PerformanceCounter() {}

void PerformanceCounter::StartCounter(const std::string& name) {
    if (!enabled_) return;
    std::lock_guard lock(mutex_);
    active_counters_[name] = std::chrono::steady_clock::now();
}

void PerformanceCounter::StopCounter(const std::string& name) {
    if (!enabled_) return;

    std::lock_guard lock(mutex_);

    auto it = active_counters_.find(name);
    if (it == active_counters_.end()) return;

    auto elapsed = std::chrono::steady_clock::now() - it->second;
    double elapsed_ms = std::chrono::duration<double, std::milli>(elapsed).count();
    
    UpdateMetric(name, elapsed_ms);
    active_counters_.erase(it);
}

void PerformanceCounter::ResetCounter(const std::string& name) {
    std::lock_guard lock(mutex_);
    metrics_.erase(name);
    active_counters_.erase(name);
}

void PerformanceCounter::ResetAll() {
    std::lock_guard lock(mutex_);
    metrics_.clear();
    active_counters_.clear();
}

PerformanceMetric PerformanceCounter::GetMetric(const std::string& name) const {
    std::lock_guard lock(mutex_);
    auto it = metrics_.find(name);
    if (it != metrics_.end()) {
        return it->second;
    }
    return PerformanceMetric();
}

std::vector<PerformanceMetric> PerformanceCounter::GetAllMetrics() const {
    std::lock_guard lock(mutex_);
    std::vector<PerformanceMetric> all_metrics;
    for (const auto& pair : metrics_) {
        all_metrics.push_back(pair.second);
    }
    return all_metrics;
}

double PerformanceCounter::GetTotalTimeMS(const std::string& name) const {
    auto metric = GetMetric(name);
    return metric.total_ms;
}

double PerformanceCounter::GetAverageTimeMS(const std::string& name) const {
    auto metric = GetMetric(name);
    return metric.avg_ms;
}

uint64_t PerformanceCounter::GetCallCount(const std::string& name) const {
    auto metric = GetMetric(name);
    return metric.count;
}

std::string PerformanceCounter::GetReport() const {
    std::lock_guard lock(mutex_);
    std::string report;
    report += "=== Performance Report ===\n";
    
    for (const auto& pair : metrics_) {
        const auto& metric = pair.second;
        if (metric.count == 0) {
            continue;
        }
        report += metric.name + ":\n";
        report += "  Calls: " + std::to_string(metric.count) + "\n";
        report += "  Total: " + std::to_string(metric.total_ms) + "ms\n";
        report += "  Avg:   " + std::to_string(metric.avg_ms) + "ms\n";
        report += "  Min:   " + std::to_string(metric.min_ms) + "ms\n";
        report += "  Max:   " + std::to_string(metric.max_ms) + "ms\n\n";
    }
    
    return report;
}

std::string PerformanceCounter::GetMetricReport(const std::string& name) const {
    std::lock_guard lock(mutex_);
    auto it = metrics_.find(name);
    if (it == metrics_.end()) {
        return "";
    }

    const auto& metric = it->second;
    if (metric.count == 0) return "";

    std::string report;
    report += name + ":\n";
    report += "  Calls: " + std::to_string(metric.count) + "\n";
    report += "  Total: " + std::to_string(metric.total_ms) + "ms\n";
    report += "  Avg:   " + std::to_string(metric.avg_ms) + "ms\n";
    report += "  Min:   " + std::to_string(metric.min_ms) + "ms\n";
    report += "  Max:   " + std::to_string(metric.max_ms) + "ms\n\n";
    
    return report;
}

void PerformanceCounter::UpdateMetric(const std::string& name, double elapsed_ms) {
    auto it = metrics_.find(name);
    
    if (it == metrics_.end()) {
        PerformanceMetric metric;
        metric.name = name;
        metric.count = 1;
        metric.total_ms = elapsed_ms;
        metric.min_ms = elapsed_ms;
        metric.max_ms = elapsed_ms;
        metric.avg_ms = elapsed_ms;
        metric.last_measured_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        
        metrics_[name] = metric;
    } else {
        auto& metric = it->second;
        metric.count++;
        metric.total_ms += elapsed_ms;
        metric.min_ms = std::min(metric.min_ms, elapsed_ms);
        metric.max_ms = std::max(metric.max_ms, elapsed_ms);
        metric.avg_ms = metric.total_ms / metric.count;
        metric.last_measured_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }
}

ScopedTimer::ScopedTimer(PerformanceCounter& counter, const std::string& name)
    : counter_(&counter), name_(name),
      start_(std::chrono::steady_clock::now()), stopped_(false) {}

ScopedTimer::~ScopedTimer() {
    if (!stopped_) Stop();
}

void ScopedTimer::Stop() {
    if (stopped_) return;
    stopped_ = true;
    const auto end = std::chrono::steady_clock::now();
    const double elapsed_ms =
        std::chrono::duration<double, std::milli>(end - start_).count();
    counter_->RecordElapsed(name_, elapsed_ms);
}

void PerformanceCounter::RecordElapsed(const std::string& name, double elapsed_ms) {
    if (!enabled_) return;
    std::lock_guard lock(mutex_);
    UpdateMetric(name, elapsed_ms);
}

}  // namespace AsyncIO::IO::LogDebug
