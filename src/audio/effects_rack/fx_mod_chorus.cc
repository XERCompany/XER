#include "fx_mod_chorus.h"

#include <algorithm>
#include <cmath>
#include <future>

namespace XER::Engine::Audio::FX {

std::string ModChorus::GetMemoryStats() const {
	std::string stats;
	stats += "DelayLine: " + std::to_string(delay_line_.GetReport().size()) + " bytes (report string)\n";
	return stats;
}

namespace {
constexpr float kPi = 3.14159265358979323846f;
// Chorus sits in the 15-45 ms range; the LFO sweeps around a 20 ms centre.
constexpr float kBaseDelayMs = 20.0f;
}

ModChorus::ModChorus()
	: sample_rate_(44100.0f),
	  rate_hz_(0.6f),
	  depth_samples_(18.0f),
	  mix_(0.35f),
	  lfo_phase_(0.0f) {
	perf_counter_.Enable();
}

ModChorus::~ModChorus() = default;

bool ModChorus::Initialize(float sample_rate, size_t max_delay_samples) {
	if (sample_rate <= 0.0f || max_delay_samples == 0) {
		return false;
	}
	sample_rate_ = sample_rate;
	lfo_phase_ = 0.0f;
	return delay_line_.Initialize(max_delay_samples);
}

void ModChorus::SetRateHz(float rate_hz) {
	rate_hz_ = std::max(0.01f, rate_hz);
}

void ModChorus::SetDepthSamples(float depth_samples) {
	depth_samples_ = std::max(0.5f, depth_samples);
}

void ModChorus::SetMix(float mix) {
	mix_ = std::clamp(mix, 0.0f, 1.0f);
}

bool ModChorus::ProcessBlock(const float* input, size_t frame_count, float* output) {
	if (input == nullptr || output == nullptr) {
		return false;
	}
	perf_counter_.StartCounter("fx_chorus");
	const float phase_inc = 2.0f * kPi * rate_hz_ / sample_rate_;
	const float base_delay = kBaseDelayMs * 0.001f * sample_rate_;
	const float max_delay = static_cast<float>(delay_line_.GetMaxDelaySamples());

	for (size_t i = 0; i < frame_count; ++i) {
		// Raised-cosine LFO is cheaper than sin() per sample and just as
		// smooth for a modulation source.
		const float lfo = 0.5f - 0.5f * std::cos(lfo_phase_);
		const float delay = std::clamp(base_delay + lfo * depth_samples_,
		                               1.0f, max_delay - 1.0f);
		const float wet = delay_line_.ProcessFractional(input[i], delay);
		output[i] = input[i] * (1.0f - mix_) + wet * mix_;
		lfo_phase_ += phase_inc;
		if (lfo_phase_ > 2.0f * kPi) {
			lfo_phase_ -= 2.0f * kPi;
		}
	}
	perf_counter_.StopCounter("fx_chorus");
	return true;
}

std::string ModChorus::GetReport() const {
	return "Chorus: rate=" + std::to_string(rate_hz_) +
		", depth=" + std::to_string(depth_samples_) +
		", mix=" + std::to_string(mix_);
}

std::future<bool> ModChorus::ProcessBlockAsync(const float* input, size_t frame_count, float* output) {
	return std::async(std::launch::async, [this, input, frame_count, output]() {
		return this->ProcessBlock(input, frame_count, output);
	});
}

}  // namespace XER::Engine::Audio::FX
