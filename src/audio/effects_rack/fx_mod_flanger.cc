#include "fx_mod_flanger.h"

#include <algorithm>
#include <cmath>
#include <future>

namespace XER::Audio::FX {

std::string ModFlanger::GetMemoryStats() const {
	std::string stats;
	stats += "DelayLine: " + std::to_string(delay_line_.GetReport().size()) + " bytes (report string)\n";
	return stats;
}

namespace {
constexpr float kPi = 3.14159265358979323846f;
}

ModFlanger::ModFlanger()
	: sample_rate_(44100.0f),
	  rate_hz_(0.25f),
	  depth_samples_(8.0f),
	  feedback_(0.3f),
	  mix_(0.5f),
	  lfo_phase_(0.0f),
	  feedback_state_(0.0f) {
	perf_counter_.Enable();
}

ModFlanger::~ModFlanger() = default;

bool ModFlanger::Initialize(float sample_rate, size_t max_delay_samples) {
	if (sample_rate <= 0.0f || max_delay_samples == 0) {
		return false;
	}
	sample_rate_ = sample_rate;
	lfo_phase_ = 0.0f;
	feedback_state_ = 0.0f;
	return delay_line_.Initialize(max_delay_samples);
}

void ModFlanger::SetRateHz(float rate_hz) {
	rate_hz_ = std::max(0.01f, rate_hz);
}

void ModFlanger::SetDepthSamples(float depth_samples) {
	depth_samples_ = std::max(1.0f, depth_samples);
}

void ModFlanger::SetFeedback(float feedback) {
	feedback_ = std::clamp(feedback, -0.95f, 0.95f);
}

void ModFlanger::SetMix(float mix) {
	mix_ = std::clamp(mix, 0.0f, 1.0f);
}

bool ModFlanger::ProcessBlock(const float* input, size_t frame_count, float* output) {
	if (input == nullptr || output == nullptr) {
		return false;
	}
	perf_counter_.StartCounter("fx_flanger");
	const float phase_inc = 2.0f * kPi * rate_hz_ / sample_rate_;
	// Flangers sweep a few milliseconds around a ~4 ms centre delay.
	const float base_delay = 4.0f * 0.001f * sample_rate_;
	const float max_delay = static_cast<float>(delay_line_.GetMaxDelaySamples());

	for (size_t i = 0; i < frame_count; ++i) {
		const float lfo = 0.5f - 0.5f * std::cos(lfo_phase_);
		const float delay = std::clamp(base_delay + lfo * depth_samples_,
		                               1.0f, max_delay - 1.0f);
		const float in = input[i] + feedback_state_ * feedback_;
		const float wet = delay_line_.ProcessFractional(in, delay);
		feedback_state_ = wet;
		output[i] = input[i] * (1.0f - mix_) + wet * mix_;
		lfo_phase_ += phase_inc;
		if (lfo_phase_ > 2.0f * kPi) {
			lfo_phase_ -= 2.0f * kPi;
		}
	}
	perf_counter_.StopCounter("fx_flanger");
	return true;
}

std::string ModFlanger::GetReport() const {
	return "Flanger: rate=" + std::to_string(rate_hz_) +
		", depth=" + std::to_string(depth_samples_) +
		", fb=" + std::to_string(feedback_) +
		", mix=" + std::to_string(mix_);
}

std::future<bool> ModFlanger::ProcessBlockAsync(const float* input, size_t frame_count, float* output) {
	return std::async(std::launch::async, [this, input, frame_count, output]() {
		return this->ProcessBlock(input, frame_count, output);
	});
}

}  // namespace XER::Audio::FX
