#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <vector>

#include "async_io/log_and_debug/io_perf_counter.h"
#include "dsp_windowing_functions.h"

namespace XER::Engine::Audio::DSP {

class GranularPitchShifter {
public:
	GranularPitchShifter();
	~GranularPitchShifter();

	bool Initialize(float sample_rate, size_t grain_size, size_t max_delay_samples);
	void SetPitchRatio(float ratio);
	float GetPitchRatio() const { return pitch_ratio_; }

	bool ProcessBlock(const float* input, size_t frame_count, float* output);
	void Reset();
	std::string GetReport() const;

private:
	struct GrainState {
		double position = 0.0;
		double delay_offset_samples = 0.0;
	};

	float ReadDelayedSample(double delay_samples) const;
	void AdvanceGrain(GrainState* grain);

	float sample_rate_;
	float pitch_ratio_;
	float smoothed_pitch_ratio_;
	size_t grain_size_;
	size_t max_delay_samples_;
	size_t write_index_;
	std::vector<float> grain_window_;
	std::vector<float> delay_buffer_;
	std::array<GrainState, 4> grains_;
	XER::AsyncIO::IO::LogDebug::PerformanceCounter perf_counter_;
};

}  // namespace XER::Engine::Audio::DSP
