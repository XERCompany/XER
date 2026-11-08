#include "metric_timing.h"

#include <numeric>

namespace XER::ModelsBuilder::TrainingMetrics {

TimingMetric::ScopedTimer::ScopedTimer(TimingMetric* metric, std::string bucket_name)
		: metric_(metric),
			bucket_name_(std::move(bucket_name)),
			start_time_(std::chrono::steady_clock::now()) {
}

TimingMetric::ScopedTimer::~ScopedTimer() {
	if (metric_ == nullptr) {
		return;
	}
	const auto end_time = std::chrono::steady_clock::now();
	const auto duration =
			std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(end_time - start_time_);
	metric_->AddDurationMs(bucket_name_, duration.count());
}

void TimingMetric::Reset() {
	durations_ms_.clear();
}

void TimingMetric::AddDurationMs(const std::string& bucket_name, double duration_ms) {
	durations_ms_[bucket_name].push_back(duration_ms);
}

size_t TimingMetric::Count(const std::string& bucket_name) const {
	const auto it = durations_ms_.find(bucket_name);
	if (it == durations_ms_.end()) {
		return 0U;
	}
	return it->second.size();
}

double TimingMetric::MeanMs(const std::string& bucket_name) const {
	const auto it = durations_ms_.find(bucket_name);
	if (it == durations_ms_.end() || it->second.empty()) {
		return 0.0;
	}
	const double total = std::accumulate(it->second.begin(), it->second.end(), 0.0);
	return total / static_cast<double>(it->second.size());
}

double TimingMetric::TotalMs(const std::string& bucket_name) const {
	const auto it = durations_ms_.find(bucket_name);
	if (it == durations_ms_.end()) {
		return 0.0;
	}
	return std::accumulate(it->second.begin(), it->second.end(), 0.0);
}

std::vector<std::string> TimingMetric::Buckets() const {
	std::vector<std::string> names;
	names.reserve(durations_ms_.size());
	for (const auto& [name, _] : durations_ms_) {
		names.push_back(name);
	}
	return names;
}

}  // namespace XER::ModelsBuilder::TrainingMetrics

