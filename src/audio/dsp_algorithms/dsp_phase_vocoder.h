#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "async_io/log_and_debug/io_perf_counter.h"

#include "dsp_stft_processor.h"

namespace XER::Engine::Audio::DSP {

class PhaseVocoder {
public:
	explicit PhaseVocoder(size_t fft_size = 1024, size_t hop_size = 256);
	~PhaseVocoder();

	bool Initialize(float sample_rate);
	void SetTimeStretchRatio(float ratio);
	float GetTimeStretchRatio() const { return time_stretch_ratio_; }

	bool ProcessBlock(const float* input, size_t frame_count, float* output);
	void Reset();
	std::string GetReport() const;

private:
	size_t fft_size_;
	size_t hop_size_;
	float sample_rate_;
	float time_stretch_ratio_;
	STFTProcessor stft_processor_;
	std::vector<float> previous_phase_;
	std::vector<float> phase_accumulator_;
	XER::AsyncIO::IO::LogDebug::PerformanceCounter perf_counter_;
};

}  // namespace XER::Engine::Audio::DSP
