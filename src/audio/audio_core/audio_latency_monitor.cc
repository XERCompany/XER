#include "audio_latency_monitor.h"

#include <algorithm>
#include <numeric>

namespace XER::Engine::Audio::Core {

LatencyMonitor::LatencyMonitor()
    : is_monitoring_(false) {}

LatencyMonitor::~LatencyMonitor() {
    StopMonitoring();
}

bool LatencyMonitor::StartMonitoring() {
    is_monitoring_ = true;
    perf_counter_.Enable();
    return true;
}

bool LatencyMonitor::StopMonitoring() {
    is_monitoring_ = false;
    perf_counter_.Disable();
    return true;
}

void LatencyMonitor::RecordInputTimestamp() {
    input_timestamp_ = std::chrono::steady_clock::now();
    perf_counter_.StartCounter("input");
}

void LatencyMonitor::RecordOutputTimestamp() {
    perf_counter_.StopCounter("input");
    output_timestamp_ = std::chrono::steady_clock::now();
}

LatencyInfo LatencyMonitor::CalculateLatency() const {
    LatencyInfo info{0.0, 0.0, 0.0, 0.0};
    
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
        output_timestamp_ - input_timestamp_);
    info.total_round_trip_ms = duration.count() / 1000.0;
    
    auto perf_metric = perf_counter_.GetMetric("input");
    info.processing_latency_ms = perf_metric.avg_ms;
    
    return info;
}

std::vector<double> LatencyMonitor::GetLatencyHistory() const {
    return latency_history_;
}

double LatencyMonitor::GetAverageLatency() const {
    if (latency_history_.empty()) return 0.0;
    return std::accumulate(latency_history_.begin(), latency_history_.end(), 0.0) / latency_history_.size();
}

double LatencyMonitor::GetMaxLatency() const {
    if (latency_history_.empty()) return 0.0;
    return *std::max_element(latency_history_.begin(), latency_history_.end());
}

double LatencyMonitor::GetMinLatency() const {
    if (latency_history_.empty()) return 0.0;
    return *std::min_element(latency_history_.begin(), latency_history_.end());
}

std::string LatencyMonitor::GetLatencyReport() const {
    std::string report;
    auto info = CalculateLatency();
    
    report += "Latency Report:\n";
    report += "Total Round Trip: " + std::to_string(info.total_round_trip_ms) + " ms\n";
    report += "Processing Latency: " + std::to_string(info.processing_latency_ms) + " ms\n";
    report += "Average: " + std::to_string(GetAverageLatency()) + " ms\n";
    report += "Min: " + std::to_string(GetMinLatency()) + " ms\n";
    report += "Max: " + std::to_string(GetMaxLatency()) + " ms\n";
    
    return report;
}

void LatencyMonitor::UpdateHistory(double latency_ms) {
    latency_history_.push_back(latency_ms);
    if (latency_history_.size() > 1000) {
        latency_history_.erase(latency_history_.begin());
    }
}

}  // namespace XER::Engine::Audio::Core
