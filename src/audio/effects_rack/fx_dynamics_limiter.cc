#include "fx_dynamics_limiter.h"

#include <algorithm>
#include <cmath>

namespace XER::Engine::Audio::FX {

namespace {
float DbToLinear(float db_value) {
	return std::pow(10.0f, db_value / 20.0f);
}
}

DynamicsLimiter::DynamicsLimiter()
	: mutex_("dynamics_limiter"),
	  ceiling_db_(-1.0f),
	  release_(0.001f),
	  gain_(1.0f) {}

DynamicsLimiter::~DynamicsLimiter() = default;

void DynamicsLimiter::SetCeilingDb(float ceiling_db) {
	XER::AsyncIO::IO::Sync::MutexWrapper::ScopedLock lock(mutex_);
	ceiling_db_ = ceiling_db;
}

void DynamicsLimiter::SetRelease(float release) {
	XER::AsyncIO::IO::Sync::MutexWrapper::ScopedLock lock(mutex_);
	release_ = std::max(0.00001f, release);
}

bool DynamicsLimiter::ProcessBlock(const float* input, size_t frame_count, float* output) {
	if (input == nullptr || output == nullptr) {
		return false;
	}

	XER::AsyncIO::IO::Sync::MutexWrapper::ScopedLock lock(mutex_);
	const float ceiling_linear = DbToLinear(ceiling_db_);

	// Fast ~1 ms attack on gain reduction, release-driven recovery —
	// smoother than instant gain jumps, still transparent on transients.
	const float attack_coeff = 1.0f - std::exp(-1.0f / (0.001f * 48000.0f));
	const float release_coeff = std::clamp(release_, 0.00001f, 0.5f);

	for (size_t i = 0; i < frame_count; ++i) {
		const float in = input[i];
		const float abs_in = std::abs(in);

		if (abs_in * gain_ > ceiling_linear && abs_in > 0.0f) {
			const float target = ceiling_linear / abs_in;
			gain_ += (target - gain_) * attack_coeff;
		} else {
			gain_ += (1.0f - gain_) * release_coeff;
		}

		output[i] = in * gain_;
	}
	return true;
}

std::string DynamicsLimiter::GetReport() const {
	return "Limiter: ceiling=" + std::to_string(ceiling_db_) +
		", release=" + std::to_string(release_);
}

}  // namespace XER::Engine::Audio::FX
