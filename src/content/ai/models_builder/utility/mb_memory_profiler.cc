#include "mb_memory_profiler.h"

#include <algorithm>
#include <fstream>

#if defined(__linux__)
#include <unistd.h>
#endif

namespace XER::ModelsBuilder::Utility {

namespace {

MemorySample ReadCurrentMemorySample() {
	MemorySample sample;
	sample.timestamp = std::chrono::steady_clock::now();

#if defined(__linux__)
	std::ifstream statm_file("/proc/self/statm");
	if (!statm_file.good()) {
		return sample;
	}

	long total_pages = 0;
	long resident_pages = 0;
	statm_file >> total_pages >> resident_pages;
	if (total_pages <= 0 || resident_pages <= 0) {
		return sample;
	}

	const long page_size = sysconf(_SC_PAGESIZE);
	if (page_size <= 0) {
		return sample;
	}

	sample.virtual_mb =
			(static_cast<double>(total_pages) * static_cast<double>(page_size)) / (1024.0 * 1024.0);
	sample.resident_mb =
			(static_cast<double>(resident_pages) * static_cast<double>(page_size)) / (1024.0 * 1024.0);
#endif

	return sample;
}

}  // namespace

void ModelMemoryProfiler::Reset() {
	samples_.clear();
}

MemorySample ModelMemoryProfiler::Capture() {
	const MemorySample sample = ReadCurrentMemorySample();
	samples_.push_back(sample);
	return sample;
}

double ModelMemoryProfiler::PeakResidentMb() const {
	double peak = 0.0;
	for (const MemorySample& sample : samples_) {
		peak = std::max(peak, sample.resident_mb);
	}
	return peak;
}

double ModelMemoryProfiler::PeakVirtualMb() const {
	double peak = 0.0;
	for (const MemorySample& sample : samples_) {
		peak = std::max(peak, sample.virtual_mb);
	}
	return peak;
}

}  // namespace XER::ModelsBuilder::Utility

