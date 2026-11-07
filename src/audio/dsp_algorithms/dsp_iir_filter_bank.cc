#include "dsp_iir_filter_bank.h"

#include <algorithm>
#include <thread>

#if defined(__AVX2__) || defined(__SSE2__)
#include <immintrin.h>
#endif

namespace XER::Engine::Audio::DSP {

IIRFilterBank::IIRFilterBank()
	: mutex_("iir_filter_bank") {}

IIRFilterBank::~IIRFilterBank() = default;

void IIRFilterBank::Clear() {
	XER::AsyncIO::IO::Sync::MutexWrapper::ScopedLock lock(mutex_);
	filters_.clear();
}

bool IIRFilterBank::AddBiquad(BiquadType type, float sample_rate, float frequency, float q) {
	BiquadProcessor biquad;
	if (!biquad.Configure(type, sample_rate, frequency, q)) {
		return false;
	}

	XER::AsyncIO::IO::Sync::MutexWrapper::ScopedLock lock(mutex_);
	filters_.push_back(biquad);
	return true;
}

bool IIRFilterBank::ProcessSample(float input, std::vector<float>& outputs) {
	XER::AsyncIO::IO::Sync::MutexWrapper::ScopedLock lock(mutex_);
	outputs.assign(filters_.size(), 0.0f);
	const size_t hardware_threads = std::max<size_t>(1, std::thread::hardware_concurrency());
	const size_t num_threads = std::min(hardware_threads, filters_.size());
	if (filters_.size() < 8 || num_threads < 2) {
#if defined(__AVX2__)
		size_t i = 0;
		for (; i + 7 < filters_.size(); i += 8) {
			float in[8];
			for (int j = 0; j < 8; ++j) in[j] = input;
			for (int j = 0; j < 8; ++j) outputs[i + j] = filters_[i + j].ProcessSample(in[j]);
		}
		for (; i < filters_.size(); ++i) {
			outputs[i] = filters_[i].ProcessSample(input);
		}
#elif defined(__SSE2__)
		size_t i = 0;
		for (; i + 3 < filters_.size(); i += 4) {
			float in[4];
			for (int j = 0; j < 4; ++j) in[j] = input;
			for (int j = 0; j < 4; ++j) outputs[i + j] = filters_[i + j].ProcessSample(in[j]);
		}
		for (; i < filters_.size(); ++i) {
			outputs[i] = filters_[i].ProcessSample(input);
		}
#else
		for (size_t index = 0; index < filters_.size(); ++index) {
			outputs[index] = filters_[index].ProcessSample(input);
		}
#endif
		return true;
	}
	std::vector<std::thread> threads(num_threads);
	auto worker = [&](size_t thread_idx) {
		size_t chunk = filters_.size() / num_threads;
		size_t start = thread_idx * chunk;
		size_t end = (thread_idx == num_threads - 1) ? filters_.size() : start + chunk;
		for (size_t i = start; i < end; ++i) {
			outputs[i] = filters_[i].ProcessSample(input);
		}
	};
	for (size_t t = 0; t < num_threads; ++t) {
		threads[t] = std::thread(worker, t);
	}
	for (auto& th : threads) th.join();
	return true;
}

std::string IIRFilterBank::GetReport() const {
	return "IIRBank: filters=" + std::to_string(filters_.size());
}

}  // namespace XER::Engine::Audio::DSP
