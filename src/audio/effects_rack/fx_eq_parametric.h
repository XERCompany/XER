#pragma once

#include <cstddef>
#include <string>
#include <vector>
#include <future>

#include "async_io/sync_primitives/mutex_wrapper.h"
#include "audio/dsp_algorithms/dsp_biquad_processor.h"

namespace XER::Engine::Audio::FX {

struct ParametricBand {
	float frequency;
	float q;
	float gain_db;
	bool enabled;
};

class ParametricEQ {
public:
	std::string GetMemoryStats() const;
	ParametricEQ();
	~ParametricEQ();

	bool Initialize(float sample_rate, size_t band_count);
	bool SetBand(size_t index, const ParametricBand& band);
	ParametricBand GetBand(size_t index) const;
	bool ProcessBlock(const float* input, size_t frame_count, float* output);
	std::future<bool> ProcessBlockAsync(const float* input, size_t frame_count, float* output);

	std::string GetReport() const;

private:
	float sample_rate_;
	XER::AsyncIO::IO::Sync::MutexWrapper mutex_;
	std::vector<ParametricBand> bands_;
	std::vector<XER::Engine::Audio::DSP::BiquadProcessor> filters_;
};

}  // namespace XER::Engine::Audio::FX
