#pragma once

#include <complex>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "async_io/log_and_debug/io_perf_counter.h"

namespace XER::Engine::Audio::DSP {

class FFTEngine {
public:
	using Complex = std::complex<float>;

	explicit FFTEngine(size_t fft_size = 1024);
	~FFTEngine();

	bool Forward(const float* input, size_t frame_count);
	bool Inverse(float* output, size_t frame_count) const;
	void Reset();

	const std::vector<Complex>& GetSpectrum() const { return spectrum_; }
	std::vector<Complex>& MutableSpectrum() { return spectrum_; }
	std::vector<float> GetMagnitude() const;
	std::string GetReport() const;

private:
	struct BackendState;

	size_t fft_size_;
	std::vector<Complex> spectrum_;
	std::unique_ptr<BackendState> backend_;
	mutable XER::AsyncIO::IO::LogDebug::PerformanceCounter perf_counter_;
};

}  // namespace XER::Engine::Audio::DSP
