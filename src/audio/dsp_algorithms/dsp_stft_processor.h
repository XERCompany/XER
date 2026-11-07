#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "dsp_fft_engine.h"
#include "dsp_windowing_functions.h"

namespace XER::Engine::Audio::DSP {

enum class STFTWindowMode {
	kHann,
	kSqrtHann,
	kHamming,
	kBlackman,
};

class STFTProcessor {
public:
	using Complex = FFTEngine::Complex;

	STFTProcessor(
		size_t fft_size = 1024,
		size_t hop_size = 256,
		STFTWindowMode analysis_window_mode = STFTWindowMode::kSqrtHann,
		STFTWindowMode synthesis_window_mode = STFTWindowMode::kSqrtHann);
	~STFTProcessor();

	bool AnalyzeFrame(const float* input, size_t frame_count);
	bool Analyze(const float* input, size_t frame_count);
	bool Synthesize(float* output, size_t frame_count);
	void RebuildHermitianSpectrum();
	void Reset();

	size_t GetFFTSize() const { return fft_size_; }
	size_t GetHopSize() const { return hop_size_; }
	size_t GetPositiveBinCount() const { return (fft_size_ / 2) + 1; }
	size_t GetLatencySamples() const { return fft_size_ > hop_size_ ? fft_size_ - hop_size_ : 0; }

	const std::vector<float>& GetAnalysisWindow() const { return analysis_window_; }
	const std::vector<float>& GetSynthesisWindow() const { return synthesis_window_; }
	const std::vector<Complex>& GetSpectrum() const { return fft_engine_.GetSpectrum(); }
	std::vector<Complex>& MutableSpectrum() { return fft_engine_.MutableSpectrum(); }
	std::string GetReport() const;

private:
	static std::vector<float> MakeWindow(STFTWindowMode mode, size_t size);

	size_t fft_size_;
	size_t hop_size_;
	FFTEngine fft_engine_;
	std::vector<float> analysis_window_;
	std::vector<float> synthesis_window_;
	std::vector<float> analysis_buffer_;
	std::vector<float> time_domain_buffer_;
	std::vector<float> overlap_buffer_;
	std::vector<float> normalization_buffer_;
};

}  // namespace XER::Engine::Audio::DSP