#include "fx_reverb_convolution.h"

#include <algorithm>
#include <vector>

namespace XER::Engine::Audio::FX {

ReverbConvolution::ReverbConvolution()
	: mix_(0.3f),
	  mutex_("fx_reverb_convolution") {}

ReverbConvolution::~ReverbConvolution() = default;

bool ReverbConvolution::LoadImpulseResponse(const float* ir, size_t ir_size) {
	XER::AsyncIO::IO::Sync::MutexWrapper::ScopedLock lock(mutex_);
	return convolution_.SetImpulseResponse(ir, ir_size);
}

void ReverbConvolution::SetMix(float mix) {
	XER::AsyncIO::IO::Sync::MutexWrapper::ScopedLock lock(mutex_);
	mix_ = std::clamp(mix, 0.0f, 1.0f);
}

bool ReverbConvolution::ProcessBlock(const float* input, size_t frame_count, float* output) {
	if (input == nullptr || output == nullptr || frame_count == 0) {
		return false;
	}

	XER::AsyncIO::IO::Sync::MutexWrapper::ScopedLock lock(mutex_);
	std::vector<float> wet(frame_count, 0.0f);
	if (!convolution_.ProcessBlock(input, frame_count, wet.data())) {
		return false;
	}

	for (size_t i = 0; i < frame_count; ++i) {
		output[i] = input[i] * (1.0f - mix_) + wet[i] * mix_;
	}
	return true;
}

std::string ReverbConvolution::GetReport() const {
	return "ReverbConv: mix=" + std::to_string(mix_) +
		", ir_size=" + std::to_string(convolution_.GetImpulseSize());
}

}  // namespace XER::Engine::Audio::FX
