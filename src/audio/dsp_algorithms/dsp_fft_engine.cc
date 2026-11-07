#include "dsp_fft_engine.h"

#include <algorithm>
#include <cmath>

#if ENGINE_HAS_MKL
#include <mkl_dfti.h>
#include <mkl_types.h>
#elif ENGINE_HAS_FFTW3F
#include <fftw3.h>
#endif

namespace XER::Engine::Audio::DSP {

namespace {
constexpr float kPi = 3.14159265358979323846f;

void ComputeForwardNaive(
	const float* input,
	size_t fft_size,
	std::vector<FFTEngine::Complex>* spectrum_out) {
	for (size_t k = 0; k < fft_size; ++k) {
		FFTEngine::Complex sum(0.0f, 0.0f);
		for (size_t n = 0; n < fft_size; ++n) {
			const float angle = -2.0f * kPi * static_cast<float>(k * n) / static_cast<float>(fft_size);
			sum += input[n] * FFTEngine::Complex(std::cos(angle), std::sin(angle));
		}
		(*spectrum_out)[k] = sum;
	}
}

void ComputeInverseNaive(
	const std::vector<FFTEngine::Complex>& spectrum,
	size_t fft_size,
	float* output) {
	for (size_t n = 0; n < fft_size; ++n) {
		FFTEngine::Complex sum(0.0f, 0.0f);
		for (size_t k = 0; k < fft_size; ++k) {
			const float angle = 2.0f * kPi * static_cast<float>(k * n) / static_cast<float>(fft_size);
			sum += spectrum[k] * FFTEngine::Complex(std::cos(angle), std::sin(angle));
		}
		output[n] = sum.real() / static_cast<float>(fft_size);
	}
}
}

#if ENGINE_HAS_MKL
struct FFTEngine::BackendState {
	BackendState() = default;

	~BackendState() {
		if (handle_ != nullptr) {
			DftiFreeDescriptor(&handle_);
		}
	}

	DFTI_DESCRIPTOR_HANDLE handle_ = nullptr;
	std::vector<MKL_Complex8> time_domain_;
	std::vector<MKL_Complex8> frequency_domain_;
};
#elif ENGINE_HAS_FFTW3F
struct FFTEngine::BackendState {
	BackendState() = default;

	~BackendState() {
		if (forward_plan != nullptr) {
			fftwf_destroy_plan(forward_plan);
		}
		if (inverse_plan != nullptr) {
			fftwf_destroy_plan(inverse_plan);
		}
		if (time_domain != nullptr) {
			fftwf_free(time_domain);
		}
		if (frequency_domain != nullptr) {
			fftwf_free(frequency_domain);
		}
	}

	fftwf_complex* time_domain = nullptr;
	fftwf_complex* frequency_domain = nullptr;
	fftwf_plan forward_plan = nullptr;
	fftwf_plan inverse_plan = nullptr;
};
#else
struct FFTEngine::BackendState {};
#endif

FFTEngine::FFTEngine(size_t fft_size)
	: fft_size_(fft_size), spectrum_(fft_size) {
	if (fft_size_ > 0) {
#if ENGINE_HAS_MKL
		backend_ = std::make_unique<BackendState>();
		backend_->time_domain_.resize(fft_size_);
		backend_->frequency_domain_.resize(fft_size_);
		DFTI_DESCRIPTOR_HANDLE handle = nullptr;
		if (DftiCreateDescriptor(&handle, DFTI_SINGLE, DFTI_COMPLEX, 1,
		                          static_cast<MKL_LONG>(fft_size_)) == DFTI_NO_ERROR &&
		    DftiCommitDescriptor(handle) == DFTI_NO_ERROR) {
			backend_->handle_ = handle;
		} else {
			if (handle != nullptr) {
				DftiFreeDescriptor(&handle);
			}
			backend_.reset();
		}
#elif ENGINE_HAS_FFTW3F
		backend_ = std::make_unique<BackendState>();
		backend_->time_domain = fftwf_alloc_complex(static_cast<int>(fft_size_));
		backend_->frequency_domain = fftwf_alloc_complex(static_cast<int>(fft_size_));
		if (backend_->time_domain == nullptr || backend_->frequency_domain == nullptr) {
			backend_.reset();
		} else {
			backend_->forward_plan = fftwf_plan_dft_1d(
				static_cast<int>(fft_size_),
				backend_->time_domain,
				backend_->frequency_domain,
				FFTW_FORWARD,
				FFTW_ESTIMATE);
			backend_->inverse_plan = fftwf_plan_dft_1d(
				static_cast<int>(fft_size_),
				backend_->frequency_domain,
				backend_->time_domain,
				FFTW_BACKWARD,
				FFTW_ESTIMATE);
			if (backend_->forward_plan == nullptr || backend_->inverse_plan == nullptr) {
				backend_.reset();
			}
		}
#endif
	}
	perf_counter_.Enable();
}

FFTEngine::~FFTEngine() = default;

bool FFTEngine::Forward(const float* input, size_t frame_count) {
	if (input == nullptr || fft_size_ == 0 || frame_count != fft_size_) {
		return false;
	}

	perf_counter_.StartCounter("fft_forward");

#if ENGINE_HAS_MKL
	if (backend_ != nullptr && backend_->handle_ != nullptr) {
		for (size_t i = 0; i < fft_size_; ++i) {
			backend_->time_domain_[i].real = input[i];
			backend_->time_domain_[i].imag = 0.0f;
		}

		if (DftiComputeForward(backend_->handle_, backend_->time_domain_.data(),
		                       backend_->frequency_domain_.data()) == DFTI_NO_ERROR) {
			for (size_t i = 0; i < fft_size_; ++i) {
				spectrum_[i] = Complex(backend_->frequency_domain_[i].real,
				                      backend_->frequency_domain_[i].imag);
			}
		} else {
			ComputeForwardNaive(input, fft_size_, &spectrum_);
		}
	} else {
		ComputeForwardNaive(input, fft_size_, &spectrum_);
	}
#elif ENGINE_HAS_FFTW3F
	if (backend_ != nullptr && backend_->forward_plan != nullptr) {
		for (size_t i = 0; i < fft_size_; ++i) {
			backend_->time_domain[i][0] = input[i];
			backend_->time_domain[i][1] = 0.0f;
		}

		fftwf_execute(backend_->forward_plan);
		for (size_t i = 0; i < fft_size_; ++i) {
			spectrum_[i] = Complex(backend_->frequency_domain[i][0], backend_->frequency_domain[i][1]);
		}
	} else {
		ComputeForwardNaive(input, fft_size_, &spectrum_);
	}
#else
	ComputeForwardNaive(input, fft_size_, &spectrum_);
#endif

	perf_counter_.StopCounter("fft_forward");
	return true;
}

bool FFTEngine::Inverse(float* output, size_t frame_count) const {
	if (output == nullptr || fft_size_ == 0 || frame_count != fft_size_) {
		return false;
	}

#if ENGINE_HAS_MKL
	if (backend_ != nullptr && backend_->handle_ != nullptr) {
		perf_counter_.StartCounter("fft_inverse");
		for (size_t i = 0; i < fft_size_; ++i) {
			backend_->frequency_domain_[i].real = spectrum_[i].real();
			backend_->frequency_domain_[i].imag = spectrum_[i].imag();
		}

		// MKL backward transform is unnormalized, same as FFTW — scale by N.
		if (DftiComputeBackward(backend_->handle_, backend_->frequency_domain_.data(),
		                        backend_->time_domain_.data()) == DFTI_NO_ERROR) {
			const float scale = 1.0f / static_cast<float>(fft_size_);
			for (size_t i = 0; i < fft_size_; ++i) {
				output[i] = backend_->time_domain_[i].real * scale;
			}
			perf_counter_.StopCounter("fft_inverse");
			return true;
		}
	}
#elif ENGINE_HAS_FFTW3F
	if (backend_ != nullptr && backend_->inverse_plan != nullptr) {
		perf_counter_.StartCounter("fft_inverse");
		for (size_t i = 0; i < fft_size_; ++i) {
			backend_->frequency_domain[i][0] = spectrum_[i].real();
			backend_->frequency_domain[i][1] = spectrum_[i].imag();
		}

		fftwf_execute(backend_->inverse_plan);
		for (size_t i = 0; i < fft_size_; ++i) {
			output[i] = backend_->time_domain[i][0] / static_cast<float>(fft_size_);
		}
		perf_counter_.StopCounter("fft_inverse");
		return true;
	}
#endif

	perf_counter_.StartCounter("fft_inverse");
	ComputeInverseNaive(spectrum_, fft_size_, output);
	perf_counter_.StopCounter("fft_inverse");
	return true;
}

void FFTEngine::Reset() {
	std::fill(spectrum_.begin(), spectrum_.end(), Complex(0.0f, 0.0f));

#if ENGINE_HAS_MKL
	if (backend_ != nullptr) {
		for (size_t i = 0; i < fft_size_; ++i) {
			backend_->time_domain_[i].real = 0.0f;
			backend_->time_domain_[i].imag = 0.0f;
			backend_->frequency_domain_[i].real = 0.0f;
			backend_->frequency_domain_[i].imag = 0.0f;
		}
	}
#elif ENGINE_HAS_FFTW3F
	if (backend_ != nullptr) {
		for (size_t i = 0; i < fft_size_; ++i) {
			backend_->time_domain[i][0] = 0.0f;
			backend_->time_domain[i][1] = 0.0f;
			backend_->frequency_domain[i][0] = 0.0f;
			backend_->frequency_domain[i][1] = 0.0f;
		}
	}
#endif
}

std::vector<float> FFTEngine::GetMagnitude() const {
	std::vector<float> magnitude(spectrum_.size(), 0.0f);
	for (size_t i = 0; i < spectrum_.size(); ++i) {
		magnitude[i] = std::abs(spectrum_[i]);
	}
	return magnitude;
}

std::string FFTEngine::GetReport() const {
	const char* backend_name = "naive_dft";
#if ENGINE_HAS_MKL
	if (backend_ != nullptr && backend_->handle_ != nullptr) {
		backend_name = "mkl_dfti";
	}
#elif ENGINE_HAS_FFTW3F
	if (backend_ != nullptr) {
		backend_name = "fftw3f";
	}
#endif
	return "FFTEngine: size=" + std::to_string(fft_size_) + ", backend=" + backend_name;
}

}  // namespace Audio::DSP
