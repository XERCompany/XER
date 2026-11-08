#include "fx_eq_parametric.h"

#include <algorithm>
#include <cmath>
#include <future>
#include <vector>

namespace XER::Audio::FX {

std::string ParametricEQ::GetMemoryStats() const {
	std::string stats;
	stats += "Bands: " + std::to_string(bands_.size() * sizeof(ParametricBand)) + " bytes\n";
	stats += "Filters: " + std::to_string(filters_.size() * sizeof(XER::Audio::DSP::BiquadProcessor)) + " bytes\n";
	return stats;
}

namespace {
float DbToLinear(float db_value) {
	return std::pow(10.0f, db_value / 20.0f);
}
}

ParametricEQ::ParametricEQ()
	: sample_rate_(44100.0f),
	  mutex_("parametric_eq") {}

ParametricEQ::~ParametricEQ() = default;

bool ParametricEQ::Initialize(float sample_rate, size_t band_count) {
	if (sample_rate <= 0.0f || band_count == 0) {
		return false;
	}

	XER::AsyncIO::IO::Sync::MutexWrapper::ScopedLock lock(mutex_);
	sample_rate_ = sample_rate;
	bands_.assign(band_count, ParametricBand{1000.0f, 1.0f, 0.0f, true});
	filters_.assign(band_count, XER::Audio::DSP::BiquadProcessor());

	for (size_t i = 0; i < band_count; ++i) {
		filters_[i].Configure(XER::Audio::DSP::BiquadType::BandPass, sample_rate_, bands_[i].frequency, bands_[i].q);
	}
	return true;
}

bool ParametricEQ::SetBand(size_t index, const ParametricBand& band) {
	XER::AsyncIO::IO::Sync::MutexWrapper::ScopedLock lock(mutex_);
	if (index >= bands_.size()) {
		return false;
	}

	bands_[index] = band;
	return filters_[index].Configure(XER::Audio::DSP::BiquadType::BandPass, sample_rate_, band.frequency, band.q);
}

ParametricBand ParametricEQ::GetBand(size_t index) const {
	if (index >= bands_.size()) {
		return ParametricBand{0.0f, 1.0f, 0.0f, false};
	}
	return bands_[index];
}

bool ParametricEQ::ProcessBlock(const float* input, size_t frame_count, float* output) {
	if (input == nullptr || output == nullptr) {
		return false;
	}

	XER::AsyncIO::IO::Sync::MutexWrapper::ScopedLock lock(mutex_);
	std::vector<float> band_gains(bands_.size(), 0.0f);
	for (size_t band = 0; band < bands_.size(); ++band) {
		if (bands_[band].enabled) {
			band_gains[band] = DbToLinear(bands_[band].gain_db) - 1.0f;
		}
	}

	for (size_t frame_index = 0; frame_index < frame_count; ++frame_index) {
		float sum = input[frame_index];
		for (size_t band = 0; band < bands_.size(); ++band) {
			if (!bands_[band].enabled) continue;
			const float filtered = filters_[band].ProcessSample(input[frame_index]);
			sum += filtered * band_gains[band];
		}
		output[frame_index] = sum;
	}
	return true;
}

std::string ParametricEQ::GetReport() const {
	return "ParametricEQ: bands=" + std::to_string(bands_.size()) +
		", sr=" + std::to_string(sample_rate_);
}


std::future<bool> ParametricEQ::ProcessBlockAsync(const float* input, size_t frame_count, float* output) {
	return std::async(std::launch::async, [this, input, frame_count, output]() {
		return this->ProcessBlock(input, frame_count, output);
	});
}

}  // namespace XER::Audio::FX
