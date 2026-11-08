#include "fx_distortion_tube.h"

#include <algorithm>
#include <cmath>

namespace XER::Audio::FX {

DistortionTube::DistortionTube()
	: drive_(2.0f),
	  output_gain_(0.8f) {
	perf_counter_.Enable();
}

DistortionTube::~DistortionTube() = default;

void DistortionTube::SetDrive(float drive) {
	drive_ = std::max(0.1f, drive);
}

void DistortionTube::SetOutputGain(float gain) {
	output_gain_ = std::max(0.0f, gain);
}

bool DistortionTube::ProcessBlock(const float* input, size_t frame_count, float* output) {
	if (input == nullptr || output == nullptr) {
		return false;
	}

	perf_counter_.StartCounter("fx_tube");
	for (size_t i = 0; i < frame_count; ++i) {
		const float x = input[i] * drive_;
		const float y = std::tanh(x);
		output[i] = y * output_gain_;
	}
	perf_counter_.StopCounter("fx_tube");
	return true;
}

std::string DistortionTube::GetReport() const {
	return "Tube: drive=" + std::to_string(drive_) +
		", gain=" + std::to_string(output_gain_);
}

}  // namespace XER::Audio::FX
