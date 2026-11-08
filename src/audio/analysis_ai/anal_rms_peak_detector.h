#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "async_io/async_buffer_pool.h"
#include "async_io/log_and_debug/io_memory_tracker.h"
#include "async_io/log_and_debug/io_perf_counter.h"

namespace XER::Audio::AnalysisAI {

class RMSPeakDetector {
public:
	RMSPeakDetector();
	~RMSPeakDetector();

	bool Analyze(const float* audio, size_t frame_count, int channels = 1);
	void Reset();

	float GetRMS() const { return rms_; }
	float GetPeak() const { return peak_; }
	float GetCrestFactor() const;
	float GetPeakDBFS() const;
	const std::vector<float>& GetWindowHistory() const { return window_history_; }
	std::string GetReport() const;

private:
	XER::AsyncIO::IO::Async::AsyncBufferPool buffer_pool_;
	XER::AsyncIO::IO::LogDebug::MemoryTracker memory_tracker_;
	XER::AsyncIO::IO::LogDebug::PerformanceCounter perf_counter_;
	std::vector<float> window_history_;
	float rms_;
	float peak_;
};

}  // namespace XER::Audio::AnalysisAI
