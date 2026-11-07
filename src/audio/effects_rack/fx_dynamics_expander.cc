#include "fx_dynamics_expander.h"

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
}

DynamicsExpander::DynamicsExpander()
	: mutex_("dynamics_expander"),
	  threshold_db_(-40.0f),
	  ratio_(2.0f),
	  range_db_(18.0f) {}

DynamicsExpander::~DynamicsExpander() = default;

void DynamicsExpander::SetThresholdDb(float threshold_db) {
	XER::AsyncIO::IO::Sync::MutexWrapper::ScopedLock lock(mutex_);
	threshold_db_ = threshold_db;
}

void DynamicsExpander::SetRatio(float ratio) {
	XER::AsyncIO::IO::Sync::MutexWrapper::ScopedLock lock(mutex_);
	ratio_ = std::max(1.0f, ratio);
}

void DynamicsExpander::SetRangeDb(float range_db) {
	XER::AsyncIO::IO::Sync::MutexWrapper::ScopedLock lock(mutex_);
	range_db_ = std::max(0.0f, range_db);
}

bool DynamicsExpander::ProcessBlock(const float* input, size_t frame_count, float* output) {
	if (input == nullptr || output == nullptr) {
		return false;
	}

	XER::AsyncIO::IO::Sync::MutexWrapper::ScopedLock lock(mutex_);
	for (size_t i = 0; i < frame_count; ++i) {
		const float in = input[i];
		const float level_db = LinearToDb(std::abs(in));

		float gain_db = 0.0f;
		if (level_db < threshold_db_) {
			const float below = threshold_db_ - level_db;
			gain_db = -std::min(range_db_, below * (ratio_ - 1.0f));
		}

		output[i] = in * DbToLinear(gain_db);
	}
	return true;
}

std::string DynamicsExpander::GetReport() const {
	return "Expander: th=" + std::to_string(threshold_db_) +
		", ratio=" + std::to_string(ratio_) +
		", range=" + std::to_string(range_db_);
}

}  // namespace XER::Engine::Audio::FX
