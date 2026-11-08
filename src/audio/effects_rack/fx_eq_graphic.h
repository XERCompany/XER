#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "async_io/log_and_debug/io_perf_counter.h"
#include "audio/dsp_algorithms/dsp_biquad_processor.h"

namespace XER::Audio::FX {

class GraphicEQ {
public:
	GraphicEQ();
	~GraphicEQ();

	bool Initialize(float sample_rate, const std::vector<float>& center_frequencies);
	void SetBandGainDb(size_t band_index, float gain_db);
	bool ProcessBlock(const float* input, size_t frame_count, float* output);

	std::string GetReport() const;

private:
	float sample_rate_;
	std::vector<float> center_frequencies_;
	std::vector<float> gains_db_;
	std::vector<XER::Audio::DSP::BiquadProcessor> filters_;
	XER::AsyncIO::IO::LogDebug::PerformanceCounter perf_counter_;
};

}  // namespace XER::Audio::FX
