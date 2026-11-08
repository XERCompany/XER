#pragma once

#include <cstddef>
#include <string>

#include "async_io/log_and_debug/io_perf_counter.h"

namespace XER::Audio::FX {

class DistortionBitcrush {
public:
	DistortionBitcrush();
	~DistortionBitcrush();

	void SetBitDepth(int bits);
	void SetDownsampleFactor(int factor);
	bool ProcessBlock(const float* input, size_t frame_count, float* output);

	std::string GetReport() const;

private:
	int bit_depth_;
	int downsample_factor_;
	int downsample_counter_;
	float held_sample_;
	XER::AsyncIO::IO::LogDebug::PerformanceCounter perf_counter_;
};

}  // namespace XER::Audio::FX
