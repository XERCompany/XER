#include "fx_distortion_bitcrush.h"

#include <algorithm>
#include <cmath>

namespace XER::Engine::Audio::FX {

DistortionBitcrush::DistortionBitcrush()
	: bit_depth_(8),
	  downsample_factor_(1),
	  downsample_counter_(0),
	  held_sample_(0.0f) {
	perf_counter_.Enable();
}

DistortionBitcrush::~DistortionBitcrush() = default;

void DistortionBitcrush::SetBitDepth(int bits) {
	bit_depth_ = std::max(1, std::min(bits, 24));
}

void DistortionBitcrush::SetDownsampleFactor(int factor) {
	downsample_factor_ = std::max(1, factor);
}

bool DistortionBitcrush::ProcessBlock(const float* input, size_t frame_count, float* output) {
	if (input == nullptr || output == nullptr) {
		return false;
	}

	perf_counter_.StartCounter("fx_bitcrush");

	const float levels = static_cast<float>(1 << std::min(bit_depth_, 16));
	for (size_t i = 0; i < frame_count; ++i) {
		if (downsample_counter_ == 0) {
			const float normalized = (input[i] + 1.0f) * 0.5f;
			const float quantized = std::floor(normalized * levels) / levels;
			held_sample_ = quantized * 2.0f - 1.0f;
		}

		output[i] = held_sample_;
		downsample_counter_ = (downsample_counter_ + 1) % downsample_factor_;
	}

	perf_counter_.StopCounter("fx_bitcrush");
	return true;
}

std::string DistortionBitcrush::GetReport() const {
	return "Bitcrush: bits=" + std::to_string(bit_depth_) +
		", downsample=" + std::to_string(downsample_factor_);
}

}  // namespace XER::Engine::Audio::FX
