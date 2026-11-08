#include "fx_eq_graphic.h"

#include <algorithm>
#include <cmath>

namespace XER::Audio::FX {

namespace {
float DbToLinear(float db_value) {
	return std::pow(10.0f, db_value / 20.0f);
}
}

GraphicEQ::GraphicEQ()
	: sample_rate_(44100.0f) {
	perf_counter_.Enable();
}

GraphicEQ::~GraphicEQ() = default;

bool GraphicEQ::Initialize(float sample_rate, const std::vector<float>& center_frequencies) {
	if (sample_rate <= 0.0f || center_frequencies.empty()) {
		return false;
	}

	sample_rate_ = sample_rate;
	center_frequencies_ = center_frequencies;
	gains_db_.assign(center_frequencies_.size(), 0.0f);
	filters_.assign(center_frequencies_.size(), XER::Audio::DSP::BiquadProcessor());

	for (size_t i = 0; i < filters_.size(); ++i) {
		filters_[i].Configure(XER::Audio::DSP::BiquadType::BandPass, sample_rate_, center_frequencies_[i], 0.8f);
	}
	return true;
}

void GraphicEQ::SetBandGainDb(size_t band_index, float gain_db) {
	if (band_index >= gains_db_.size()) {
		return;
	}
	gains_db_[band_index] = gain_db;
}

bool GraphicEQ::ProcessBlock(const float* input, size_t frame_count, float* output) {
	if (input == nullptr || output == nullptr) {
		return false;
	}
	perf_counter_.StartCounter("fx_graphic_eq");
	for (size_t n = 0; n < frame_count; ++n) {
		float sum = input[n];
		for (size_t band = 0; band < filters_.size(); ++band) {
			const float band_signal = filters_[band].ProcessSample(input[n]);
			sum += band_signal * (DbToLinear(gains_db_[band]) - 1.0f);
		}
		output[n] = sum;
	}
	perf_counter_.StopCounter("fx_graphic_eq");
	return true;
}

std::string GraphicEQ::GetReport() const {
	return "GraphicEQ: bands=" + std::to_string(center_frequencies_.size()) +
		", sr=" + std::to_string(sample_rate_);
}

}  // namespace XER::Audio::FX
