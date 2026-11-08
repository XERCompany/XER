#include "dsp_phase_vocoder.h"

#include <algorithm>
#include <cmath>

namespace XER::Audio::DSP {

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 6.28318530717958647692f;

float WrapPhase(float phase) {
	while (phase > kPi) {
		phase -= kTwoPi;
	}
	while (phase < -kPi) {
		phase += kTwoPi;
	}
	return phase;
}

}  // namespace

PhaseVocoder::PhaseVocoder(size_t fft_size, size_t hop_size)
	: fft_size_(fft_size),
	  hop_size_(hop_size),
	  sample_rate_(44100.0f),
	  time_stretch_ratio_(1.0f),
	  stft_processor_(fft_size, hop_size),
	  previous_phase_((fft_size / 2) + 1, 0.0f),
	  phase_accumulator_((fft_size / 2) + 1, 0.0f) {
	perf_counter_.Enable();
}

PhaseVocoder::~PhaseVocoder() = default;

bool PhaseVocoder::Initialize(float sample_rate) {
	if (sample_rate <= 0.0f) {
		return false;
	}
	sample_rate_ = sample_rate;
	Reset();
	return true;
}

void PhaseVocoder::SetTimeStretchRatio(float ratio) {
	if (ratio < 0.25f) {
		time_stretch_ratio_ = 0.25f;
	} else if (ratio > 4.0f) {
		time_stretch_ratio_ = 4.0f;
	} else {
		time_stretch_ratio_ = ratio;
	}
}

bool PhaseVocoder::ProcessBlock(const float* input, size_t frame_count, float* output) {
	if (input == nullptr || output == nullptr || hop_size_ == 0 || frame_count == 0 || (frame_count % hop_size_) != 0) {
		return false;
	}

	perf_counter_.StartCounter("phase_vocoder");
	const size_t positive_bins = stft_processor_.GetPositiveBinCount();
	const float analysis_hop = static_cast<float>(hop_size_);
	const float synthesis_hop = analysis_hop * time_stretch_ratio_;
	for (size_t offset = 0; offset < frame_count; offset += hop_size_) {
		if (!stft_processor_.Analyze(input + offset, hop_size_)) {
			perf_counter_.StopCounter("phase_vocoder");
			return false;
		}

		auto& spectrum = stft_processor_.MutableSpectrum();
		for (size_t bin = 0; bin < positive_bins; ++bin) {
			const float magnitude = std::abs(spectrum[bin]);
			const float phase = std::arg(spectrum[bin]);
			const float expected_advance = kTwoPi * static_cast<float>(bin) * analysis_hop / static_cast<float>(fft_size_);
			const float delta = WrapPhase(phase - previous_phase_[bin] - expected_advance);
			const float true_frequency = (kTwoPi * static_cast<float>(bin) / static_cast<float>(fft_size_))
				+ (delta / std::max(analysis_hop, 1.0f));

			phase_accumulator_[bin] += true_frequency * synthesis_hop;
			previous_phase_[bin] = phase;
			spectrum[bin] = std::polar(magnitude, phase_accumulator_[bin]);
		}

		if (!stft_processor_.Synthesize(output + offset, hop_size_)) {
			perf_counter_.StopCounter("phase_vocoder");
			return false;
		}
	}

	perf_counter_.StopCounter("phase_vocoder");
	return true;
}

void PhaseVocoder::Reset() {
	std::fill(previous_phase_.begin(), previous_phase_.end(), 0.0f);
	std::fill(phase_accumulator_.begin(), phase_accumulator_.end(), 0.0f);
	stft_processor_.Reset();
}

std::string PhaseVocoder::GetReport() const {
	return "PhaseVocoder: fft=" + std::to_string(fft_size_) +
		", hop=" + std::to_string(hop_size_) +
		", stretch=" + std::to_string(time_stretch_ratio_) +
		", stft_latency=" + std::to_string(stft_processor_.GetLatencySamples());
}

}  // namespace XER::Audio::DSP
