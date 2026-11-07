#include "fx_mod_extra.h"

#include <algorithm>
#include <cmath>

namespace XER::Engine::Audio::FX {
namespace {
constexpr float kPi = 3.14159265358979323846f;
}

// ---------------------------------------------------------------------------
// Tremolo
// ---------------------------------------------------------------------------

ModTremolo::ModTremolo() {
	perf_counter_.Enable();
}

ModTremolo::~ModTremolo() = default;

bool ModTremolo::Initialize(float sample_rate) {
	if (sample_rate <= 0.0f) {
		return false;
	}
	sample_rate_ = sample_rate;
	lfo_phase_ = 0.0f;
	return true;
}

void ModTremolo::SetRateHz(float rate_hz) {
	rate_hz_ = std::max(0.01f, rate_hz);
}

void ModTremolo::SetDepth(float depth) {
	depth_ = std::clamp(depth, 0.0f, 1.0f);
}

void ModTremolo::SetMix(float mix) {
	mix_ = std::clamp(mix, 0.0f, 1.0f);
}

bool ModTremolo::ProcessBlock(const float* input, size_t frame_count, float* output) {
	if (input == nullptr || output == nullptr) {
		return false;
	}
	perf_counter_.StartCounter("fx_tremolo");
	const float phase_inc = 2.0f * kPi * rate_hz_ / sample_rate_;
	// Depth scales the modulation around a 0.5 centre so depth=1 still
	// passes signal at the LFO trough instead of muting completely.
	const float mod_depth = depth_ * 0.9f;

	for (size_t i = 0; i < frame_count; ++i) {
		const float lfo = 0.5f - 0.5f * std::cos(lfo_phase_);
		const float gain = 1.0f - mod_depth * 0.5f + mod_depth * lfo;
		const float wet = input[i] * gain;
		output[i] = input[i] * (1.0f - mix_) + wet * mix_;
		lfo_phase_ += phase_inc;
		if (lfo_phase_ > 2.0f * kPi) {
			lfo_phase_ -= 2.0f * kPi;
		}
	}
	perf_counter_.StopCounter("fx_tremolo");
	return true;
}

std::string ModTremolo::GetReport() const {
	return "Tremolo: rate=" + std::to_string(rate_hz_) +
		", depth=" + std::to_string(depth_) +
		", mix=" + std::to_string(mix_);
}

// ---------------------------------------------------------------------------
// Vibrato
// ---------------------------------------------------------------------------

ModVibrato::ModVibrato() {
	perf_counter_.Enable();
}

ModVibrato::~ModVibrato() = default;

bool ModVibrato::Initialize(float sample_rate, size_t max_delay_samples) {
	if (sample_rate <= 0.0f || max_delay_samples == 0) {
		return false;
	}
	sample_rate_ = sample_rate;
	lfo_phase_ = 0.0f;
	return delay_line_.Initialize(max_delay_samples);
}

void ModVibrato::SetRateHz(float rate_hz) {
	rate_hz_ = std::max(0.01f, rate_hz);
}

void ModVibrato::SetDepthMs(float depth_ms) {
	depth_ms_ = std::max(0.1f, depth_ms);
}

void ModVibrato::SetMix(float mix) {
	mix_ = std::clamp(mix, 0.0f, 1.0f);
}

bool ModVibrato::ProcessBlock(const float* input, size_t frame_count, float* output) {
	if (input == nullptr || output == nullptr) {
		return false;
	}
	perf_counter_.StartCounter("fx_vibrato");
	const float phase_inc = 2.0f * kPi * rate_hz_ / sample_rate_;
	// Delay wobbles ±depth/2 around a 12 ms centre — enough headroom for
	// musical pitch wobble without sounding like a chorus.
	const float centre = 12.0f * 0.001f * sample_rate_;
	const float depth = depth_ms_ * 0.5f * 0.001f * sample_rate_;
	const float max_delay = static_cast<float>(delay_line_.GetMaxDelaySamples());

	for (size_t i = 0; i < frame_count; ++i) {
		const float lfo = 0.5f - 0.5f * std::cos(lfo_phase_);
		const float delay = std::clamp(centre - depth + lfo * 2.0f * depth,
		                               1.0f, max_delay - 1.0f);
		const float wet = delay_line_.ProcessFractional(input[i], delay);
		output[i] = input[i] * (1.0f - mix_) + wet * mix_;
		lfo_phase_ += phase_inc;
		if (lfo_phase_ > 2.0f * kPi) {
			lfo_phase_ -= 2.0f * kPi;
		}
	}
	perf_counter_.StopCounter("fx_vibrato");
	return true;
}

std::string ModVibrato::GetReport() const {
	return "Vibrato: rate=" + std::to_string(rate_hz_) +
		", depth_ms=" + std::to_string(depth_ms_) +
		", mix=" + std::to_string(mix_);
}

// ---------------------------------------------------------------------------
// Ring modulation
// ---------------------------------------------------------------------------

ModRingMod::ModRingMod() {
	perf_counter_.Enable();
}

ModRingMod::~ModRingMod() = default;

bool ModRingMod::Initialize(float sample_rate) {
	if (sample_rate <= 0.0f) {
		return false;
	}
	sample_rate_ = sample_rate;
	phase_ = 0.0f;
	return true;
}

void ModRingMod::SetFrequencyHz(float frequency_hz) {
	frequency_hz_ = std::clamp(frequency_hz, 0.1f, sample_rate_ * 0.45f);
}

void ModRingMod::SetMix(float mix) {
	mix_ = std::clamp(mix, 0.0f, 1.0f);
}

bool ModRingMod::ProcessBlock(const float* input, size_t frame_count, float* output) {
	if (input == nullptr || output == nullptr) {
		return false;
	}
	perf_counter_.StartCounter("fx_ringmod");
	const float phase_inc = 2.0f * kPi * frequency_hz_ / sample_rate_;

	for (size_t i = 0; i < frame_count; ++i) {
		const float carrier = std::sin(phase_);
		const float wet = input[i] * carrier;
		output[i] = input[i] * (1.0f - mix_) + wet * mix_;
		phase_ += phase_inc;
		if (phase_ > 2.0f * kPi) {
			phase_ -= 2.0f * kPi;
		}
	}
	perf_counter_.StopCounter("fx_ringmod");
	return true;
}

std::string ModRingMod::GetReport() const {
	return "RingMod: freq=" + std::to_string(frequency_hz_) +
		", mix=" + std::to_string(mix_);
}

}  // namespace XER::Engine::Audio::FX
