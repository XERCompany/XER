#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "async_io/sync_primitives/mutex_wrapper.h"

#include "dsp_stft_processor.h"

namespace XER::Engine::Audio::DSP {

class SpectralShaper {
public:
	explicit SpectralShaper(size_t fft_size = 1024, size_t hop_size = 256);
	~SpectralShaper();

	bool SetCurve(const std::vector<float>& curve);
	bool ProcessBlock(const float* input, size_t frame_count, float* output);
	void Reset();

	const std::vector<float>& GetCurve() const { return shaping_curve_; }
	std::string GetReport() const;

private:
	size_t fft_size_;
	size_t hop_size_;
	STFTProcessor stft_processor_;
	std::vector<float> shaping_curve_;
	XER::AsyncIO::IO::Sync::MutexWrapper mutex_;
};

}  // namespace XER::Engine::Audio::DSP
