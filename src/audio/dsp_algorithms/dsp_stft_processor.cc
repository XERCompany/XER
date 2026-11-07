#include "dsp_stft_processor.h"

#include <algorithm>
#include <cmath>

#if defined(__AVX2__) || defined(__SSE2__)
#include <immintrin.h>
#endif

namespace XER::Engine::Audio::DSP {

namespace {

constexpr float kNormalizationFloor = 1.0e-6f;

}  // namespace

STFTProcessor::STFTProcessor(
	size_t fft_size,
	size_t hop_size,
	STFTWindowMode analysis_window_mode,
	STFTWindowMode synthesis_window_mode)
	: fft_size_(fft_size),
	  hop_size_(std::min(hop_size == 0 ? fft_size : hop_size, fft_size)),
	  fft_engine_(fft_size),
	  analysis_window_(MakeWindow(analysis_window_mode, fft_size)),
	  synthesis_window_(MakeWindow(synthesis_window_mode, fft_size)),
	  analysis_buffer_(fft_size, 0.0f),
	  time_domain_buffer_(fft_size, 0.0f),
	  overlap_buffer_(fft_size, 0.0f),
	  normalization_buffer_(fft_size, 0.0f) {}

STFTProcessor::~STFTProcessor() = default;

bool STFTProcessor::AnalyzeFrame(const float* input, size_t frame_count) {
	if (input == nullptr || frame_count != fft_size_ || fft_size_ == 0) {
		return false;
	}

	std::copy(input, input + fft_size_, analysis_buffer_.begin());
	time_domain_buffer_ = analysis_buffer_;
	WindowingFunctions::ApplyWindow(analysis_window_, time_domain_buffer_.data(), time_domain_buffer_.size());
	return fft_engine_.Forward(time_domain_buffer_.data(), time_domain_buffer_.size());
}

bool STFTProcessor::Analyze(const float* input, size_t frame_count) {
	if (input == nullptr || frame_count != hop_size_ || fft_size_ == 0 || hop_size_ == 0) {
		return false;
	}

	if (hop_size_ < fft_size_) {
		std::move(analysis_buffer_.begin() + static_cast<std::ptrdiff_t>(hop_size_), analysis_buffer_.end(), analysis_buffer_.begin());
	}
	std::copy(input, input + hop_size_, analysis_buffer_.end() - static_cast<std::ptrdiff_t>(hop_size_));

	return AnalyzeFrame(analysis_buffer_.data(), analysis_buffer_.size());
}

bool STFTProcessor::Synthesize(float* output, size_t frame_count) {
	if (output == nullptr || frame_count != hop_size_ || fft_size_ == 0 || hop_size_ == 0) {
		return false;
	}

	RebuildHermitianSpectrum();
	if (!fft_engine_.Inverse(time_domain_buffer_.data(), time_domain_buffer_.size())) {
		return false;
	}


#if defined(__AVX2__)
	// AVX2: 8 float per vector
	size_t i = 0;
	for (; i + 7 < fft_size_; i += 8) {
		__m256 t = _mm256_loadu_ps(&time_domain_buffer_[i]);
		__m256 w = _mm256_loadu_ps(&synthesis_window_[i]);
		__m256 weighted = _mm256_mul_ps(t, w);
		__m256 o = _mm256_loadu_ps(&overlap_buffer_[i]);
		o = _mm256_add_ps(o, weighted);
		_mm256_storeu_ps(&overlap_buffer_[i], o);
		__m256 n = _mm256_loadu_ps(&normalization_buffer_[i]);
		__m256 w2 = _mm256_mul_ps(w, w);
		n = _mm256_add_ps(n, w2);
		_mm256_storeu_ps(&normalization_buffer_[i], n);
	}
	for (; i < fft_size_; ++i) {
		const float weighted = time_domain_buffer_[i] * synthesis_window_[i];
		overlap_buffer_[i] += weighted;
		normalization_buffer_[i] += synthesis_window_[i] * synthesis_window_[i];
	}
#elif defined(__SSE2__)
	// SSE2: 4 float per vector
	size_t i = 0;
	for (; i + 3 < fft_size_; i += 4) {
		__m128 t = _mm_loadu_ps(&time_domain_buffer_[i]);
		__m128 w = _mm_loadu_ps(&synthesis_window_[i]);
		__m128 weighted = _mm_mul_ps(t, w);
		__m128 o = _mm_loadu_ps(&overlap_buffer_[i]);
		o = _mm_add_ps(o, weighted);
		_mm_storeu_ps(&overlap_buffer_[i], o);
		__m128 n = _mm_loadu_ps(&normalization_buffer_[i]);
		__m128 w2 = _mm_mul_ps(w, w);
		n = _mm_add_ps(n, w2);
		_mm_storeu_ps(&normalization_buffer_[i], n);
	}
	for (; i < fft_size_; ++i) {
		const float weighted = time_domain_buffer_[i] * synthesis_window_[i];
		overlap_buffer_[i] += weighted;
		normalization_buffer_[i] += synthesis_window_[i] * synthesis_window_[i];
	}
#else
	// Fallback: std::transform
	for (size_t i = 0; i < fft_size_; ++i) {
		const float weighted = time_domain_buffer_[i] * synthesis_window_[i];
		overlap_buffer_[i] += weighted;
		normalization_buffer_[i] += synthesis_window_[i] * synthesis_window_[i];
	}
#endif

	for (size_t i = 0; i < hop_size_; ++i) {
		const float normalization = normalization_buffer_[i];
		output[i] = normalization > kNormalizationFloor ? overlap_buffer_[i] / normalization : 0.0f;
	}

	if (hop_size_ < fft_size_) {
		std::move(overlap_buffer_.begin() + static_cast<std::ptrdiff_t>(hop_size_), overlap_buffer_.end(), overlap_buffer_.begin());
		std::fill(overlap_buffer_.end() - static_cast<std::ptrdiff_t>(hop_size_), overlap_buffer_.end(), 0.0f);

		std::move(
			normalization_buffer_.begin() + static_cast<std::ptrdiff_t>(hop_size_),
			normalization_buffer_.end(),
			normalization_buffer_.begin());
		std::fill(
			normalization_buffer_.end() - static_cast<std::ptrdiff_t>(hop_size_),
			normalization_buffer_.end(),
			0.0f);
	}

	return true;
}

void STFTProcessor::RebuildHermitianSpectrum() {
	auto& spectrum = fft_engine_.MutableSpectrum();
	const size_t positive_bins = GetPositiveBinCount();
	for (size_t bin = positive_bins; bin < fft_size_; ++bin) {
		const size_t mirrored = fft_size_ - bin;
		spectrum[bin] = std::conj(spectrum[mirrored]);
	}
	if ((fft_size_ % 2u) == 0u && !spectrum.empty()) {
		spectrum[fft_size_ / 2] = Complex(spectrum[fft_size_ / 2].real(), 0.0f);
	}
	if (!spectrum.empty()) {
		spectrum[0] = Complex(spectrum[0].real(), 0.0f);
	}
}

void STFTProcessor::Reset() {
	std::fill(analysis_buffer_.begin(), analysis_buffer_.end(), 0.0f);
	std::fill(time_domain_buffer_.begin(), time_domain_buffer_.end(), 0.0f);
	std::fill(overlap_buffer_.begin(), overlap_buffer_.end(), 0.0f);
	std::fill(normalization_buffer_.begin(), normalization_buffer_.end(), 0.0f);
	fft_engine_.Reset();
}

std::string STFTProcessor::GetReport() const {
	return "STFTProcessor: fft=" + std::to_string(fft_size_)
		+ ", hop=" + std::to_string(hop_size_)
		+ ", latency=" + std::to_string(GetLatencySamples());
}

std::vector<float> STFTProcessor::MakeWindow(STFTWindowMode mode, size_t size) {
	switch (mode) {
		case STFTWindowMode::kHann:
			return WindowingFunctions::GenerateHann(size);
		case STFTWindowMode::kSqrtHann:
			return WindowingFunctions::GenerateSqrtHann(size);
		case STFTWindowMode::kHamming:
			return WindowingFunctions::GenerateHamming(size);
		case STFTWindowMode::kBlackman:
			return WindowingFunctions::GenerateBlackman(size);
	}
	return WindowingFunctions::GenerateSqrtHann(size);
}

}  // namespace XER::Engine::Audio::DSP