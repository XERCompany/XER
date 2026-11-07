#pragma once

#include <cstddef>
#include <string>
#include <future>

#include "async_io/log_and_debug/io_perf_counter.h"
#include "audio/dsp_algorithms/dsp_delay_line_circular.h"

namespace XER::Engine::Audio::FX {

class ModChorus {
public:
	std::string GetMemoryStats() const;
	ModChorus();
	~ModChorus();

	bool Initialize(float sample_rate, size_t max_delay_samples);
	void SetRateHz(float rate_hz);
	void SetDepthSamples(float depth_samples);
	void SetMix(float mix);
	bool ProcessBlock(const float* input, size_t frame_count, float* output);
	std::future<bool> ProcessBlockAsync(const float* input, size_t frame_count, float* output);

	std::string GetReport() const;

private:
	float sample_rate_;
	float rate_hz_;
	float depth_samples_;
	float mix_;
	float lfo_phase_;
	XER::Engine::Audio::DSP::CircularDelayLine delay_line_;
	XER::AsyncIO::IO::LogDebug::PerformanceCounter perf_counter_;
};

}  // namespace XER::Engine::Audio::FX
