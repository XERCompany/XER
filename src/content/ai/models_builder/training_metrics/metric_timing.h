#pragma once

#include <chrono>
#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace XER::ModelsBuilder::TrainingMetrics {

class TimingMetric {
 public:
	class ScopedTimer {
	 public:
		ScopedTimer(TimingMetric* metric, std::string bucket_name);
		~ScopedTimer();

	 private:
		TimingMetric* metric_;
		std::string bucket_name_;
		std::chrono::steady_clock::time_point start_time_;
	};

	void Reset();
	void AddDurationMs(const std::string& bucket_name, double duration_ms);

	size_t Count(const std::string& bucket_name) const;
	double MeanMs(const std::string& bucket_name) const;
	double TotalMs(const std::string& bucket_name) const;
	std::vector<std::string> Buckets() const;

 private:
	std::unordered_map<std::string, std::vector<double>> durations_ms_;
};

}  // namespace XER::ModelsBuilder::TrainingMetrics

