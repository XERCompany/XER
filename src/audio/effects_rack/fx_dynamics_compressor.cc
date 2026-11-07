#include "fx_dynamics_compressor.h"

#include <algorithm>
#include <cmath>

namespace XER::Engine::Audio::FX {

namespace {
float DbToLinear(float db_value) {
	return std::pow(10.0f, db_value / 20.0f);
}

float LinearToDb(float linear) {
	return 20.0f * std::log10(std::max(linear, 1e-9f));
}

// One-pole coefficient for a time constant in milliseconds.
float TimeCoeffMs(float time_ms, float sample_rate) {
	return std::exp(-1.0f / (std::max(time_ms, 0.01f) * 0.001f * sample_rate));
}
}

DynamicsCompressor::DynamicsCompressor()
	: mutex_("dynamics_compressor"),
	  sample_rate_(48000.0f),
	  threshold_db_(-18.0f),
	  ratio_(4.0f),
	  attack_ms_(10.0f),
	  release_ms_(120.0f),
	  envelope_(0.0f),
	  gain_db_(0.0f) {}

DynamicsCompressor::~DynamicsCompressor() = default;

void DynamicsCompressor::SetSampleRate(float sample_rate) {
	XER::AsyncIO::IO::Sync::MutexWrapper::ScopedLock lock(mutex_);
	if (sample_rate > 0.0f) {
		sample_rate_ = sample_rate;
	}
}

void DynamicsCompressor::SetThresholdDb(float threshold_db) {
	XER::AsyncIO::IO::Sync::MutexWrapper::ScopedLock lock(mutex_);
	threshold_db_ = threshold_db;
}

void DynamicsCompressor::SetRatio(float ratio) {
	XER::AsyncIO::IO::Sync::MutexWrapper::ScopedLock lock(mutex_);
	ratio_ = std::max(1.0f, ratio);
}

void DynamicsCompressor::SetAttackMs(float attack_ms) {
	XER::AsyncIO::IO::Sync::MutexWrapper::ScopedLock lock(mutex_);
	attack_ms_ = std::max(0.1f, attack_ms);
}

void DynamicsCompressor::SetReleaseMs(float release_ms) {
	XER::AsyncIO::IO::Sync::MutexWrapper::ScopedLock lock(mutex_);
	release_ms_ = std::max(1.0f, release_ms);
}

bool DynamicsCompressor::ProcessBlock(const float* input, size_t frame_count, float* output) {
	if (input == nullptr || output == nullptr) {
		return false;
	}

	XER::AsyncIO::IO::Sync::MutexWrapper::ScopedLock lock(mutex_);

	// Peak envelope with program-dependent release, then a smoothed gain
	// computer: attack/release are real time constants, so the compressor
	// behaves identically at any sample rate and never zippers.
	const float env_release = TimeCoeffMs(release_ms_ * 0.5f, sample_rate_);
	const float gain_attack = TimeCoeffMs(attack_ms_, sample_rate_);
	const float gain_release = TimeCoeffMs(release_ms_, sample_rate_);

	for (size_t i = 0; i < frame_count; ++i) {
		const float sample = input[i];
		const float abs_sample = std::abs(sample);
		envelope_ = abs_sample > envelope_ ? abs_sample : envelope_ * env_release;

		const float env_db = LinearToDb(envelope_);
		float target_gain_db = 0.0f;
		if (env_db > threshold_db_) {
			const float over_db = env_db - threshold_db_;
			target_gain_db = -(over_db - over_db / ratio_);
		}

		// Attack coefficient when gain is being reduced, release otherwise.
		const float coeff = target_gain_db < gain_db_ ? gain_attack : gain_release;
		gain_db_ += (target_gain_db - gain_db_) * (1.0f - coeff);

		output[i] = sample * DbToLinear(gain_db_);
	}
	return true;
}

std::string DynamicsCompressor::GetReport() const {
	return "Compressor: th=" + std::to_string(threshold_db_) +
		", ratio=" + std::to_string(ratio_) +
		", attack=" + std::to_string(attack_ms_) +
		", release=" + std::to_string(release_ms_);
}

}  // namespace XER::Engine::Audio::FX
