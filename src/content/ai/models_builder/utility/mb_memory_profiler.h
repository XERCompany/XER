#pragma once

#include <chrono>
#include <cstddef>
#include <vector>

namespace XER::ModelsBuilder::Utility {

struct MemorySample {
	double resident_mb = 0.0;
	double virtual_mb = 0.0;
	std::chrono::steady_clock::time_point timestamp = std::chrono::steady_clock::now();
};

class ModelMemoryProfiler {
 public:
	void Reset();
	MemorySample Capture();

	const std::vector<MemorySample>& Samples() const { return samples_; }
	size_t Count() const { return samples_.size(); }

	double PeakResidentMb() const;
	double PeakVirtualMb() const;

 private:
	std::vector<MemorySample> samples_;
};

}  // namespace XER::ModelsBuilder::Utility

