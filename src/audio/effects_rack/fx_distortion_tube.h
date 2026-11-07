#pragma once

#include <cstddef>
#include <string>

#include "async_io/log_and_debug/io_perf_counter.h"

namespace XER::Engine::Audio::FX {

class DistortionTube {
public:
	DistortionTube();
	~DistortionTube();

	void SetDrive(float drive);
	void SetOutputGain(float gain);
	bool ProcessBlock(const float* input, size_t frame_count, float* output);

	std::string GetReport() const;

private:
	float drive_;
	float output_gain_;
	XER::AsyncIO::IO::LogDebug::PerformanceCounter perf_counter_;
};

}  // namespace XER::Engine::Audio::FX
