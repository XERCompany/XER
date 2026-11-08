#include "dsp_spectral_shaper.h"

#include <algorithm>

#if defined(__AVX2__) || defined(__SSE2__)
#include <immintrin.h>
#endif

namespace XER::Audio::DSP {

SpectralShaper::SpectralShaper(size_t fft_size, size_t hop_size)
	: fft_size_(fft_size),
	  hop_size_(hop_size == 0 ? fft_size : std::min(hop_size, fft_size)),
	  stft_processor_(fft_size_, hop_size_),
	  shaping_curve_((fft_size / 2) + 1, 1.0f),
	  mutex_("spectral_shaper") {}

SpectralShaper::~SpectralShaper() = default;

bool SpectralShaper::SetCurve(const std::vector<float>& curve) {
	if (curve.empty()) {
		return false;
	}

	XER::AsyncIO::IO::Sync::MutexWrapper::ScopedLock lock(mutex_);
	shaping_curve_ = curve;
	return true;
}

bool SpectralShaper::ProcessBlock(const float* input, size_t frame_count, float* output) {
	if (input == nullptr || output == nullptr || hop_size_ == 0 || frame_count == 0 || (frame_count % hop_size_) != 0) {
		return false;
	}

	XER::AsyncIO::IO::Sync::MutexWrapper::ScopedLock lock(mutex_);
	const size_t positive_bins = stft_processor_.GetPositiveBinCount();
	for (size_t offset = 0; offset < frame_count; offset += hop_size_) {
		if (!stft_processor_.Analyze(input + offset, hop_size_)) {
			return false;
		}

		auto& spectrum = stft_processor_.MutableSpectrum();
		const size_t limit = std::min(positive_bins, shaping_curve_.size());
#if defined(__AVX2__)
		size_t i = 0;
		for (; i + 7 < limit; i += 8) {
			__m256 s = _mm256_loadu_ps(reinterpret_cast<float*>(&spectrum[i]));
			__m256 c = _mm256_loadu_ps(&shaping_curve_[i]);
			s = _mm256_mul_ps(s, c);
			_mm256_storeu_ps(reinterpret_cast<float*>(&spectrum[i]), s);
		}
		for (; i < limit; ++i) {
			spectrum[i] *= shaping_curve_[i];
		}
#elif defined(__SSE2__)
		size_t i = 0;
		for (; i + 3 < limit; i += 4) {
			__m128 s = _mm_loadu_ps(reinterpret_cast<float*>(&spectrum[i]));
			__m128 c = _mm_loadu_ps(&shaping_curve_[i]);
			s = _mm_mul_ps(s, c);
			_mm_storeu_ps(reinterpret_cast<float*>(&spectrum[i]), s);
		}
		for (; i < limit; ++i) {
			spectrum[i] *= shaping_curve_[i];
		}
#else
		for (size_t i = 0; i < limit; ++i) {
			spectrum[i] *= shaping_curve_[i];
		}
#endif

		if (!stft_processor_.Synthesize(output + offset, hop_size_)) {
			return false;
		}
	}

	return true;
}

void SpectralShaper::Reset() {
	XER::AsyncIO::IO::Sync::MutexWrapper::ScopedLock lock(mutex_);
	std::fill(shaping_curve_.begin(), shaping_curve_.end(), 1.0f);
	stft_processor_.Reset();
}

std::string SpectralShaper::GetReport() const {
	return "SpectralShaper: fft=" + std::to_string(fft_size_) +
		", hop=" + std::to_string(hop_size_) +
		", curve_size=" + std::to_string(shaping_curve_.size());
}

}  // namespace XER::Audio::DSP
