#pragma once

#include <chrono>
#include <cstddef>
#include <vector>

namespace XER::ModelsBuilder::TrainingMetrics {

struct ResourceUsageSample {
	double cpu_percent = 0.0;
	double gpu_percent = 0.0;
	double memory_mb = 0.0;
	std::chrono::steady_clock::time_point timestamp = std::chrono::steady_clock::now();
};

class CpuGpuUsageMetric {
 public:
	void Reset();
	ResourceUsageSample CaptureSample();

	const std::vector<ResourceUsageSample>& Samples() const { return samples_; }
	size_t Count() const { return samples_.size(); }

	double MeanCpuPercent() const;
	double MeanGpuPercent() const;
	double MeanMemoryMb() const;

 private:
	double QueryProcessMemoryMb() const;
	std::vector<ResourceUsageSample> samples_;
};

}  // namespace XER::ModelsBuilder::TrainingMetrics

