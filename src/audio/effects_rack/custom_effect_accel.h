#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "custom_effect_struct.h"

namespace XER::Engine::Audio::FX {

struct CustomEffectAccelerationInfo {
	bool simd_enabled = false;
	bool cuda_requested = false;
	bool cuda_available = false;
	std::string backend_tag;
};

CustomEffectAccelerationInfo ResolveCustomEffectAcceleration();
bool ResolveCustomEffectNodeConstantMix(const CustomEffectNode& node, float* mix_out);

void ApplyGainBuffer(const float* input, size_t frame_count, float gain, float* output);
void ApplyGainInPlace(std::vector<float>* samples, float gain);
void AddScaledBuffer(const float* input, size_t frame_count, float scale, float* output);
void MixDryWetConstant(const float* dry, const float* wet, size_t frame_count, float mix, float* output);
void LimitBufferInPlace(std::vector<float>* samples, float ceiling);
void SumNormalizeBuffers(const std::vector<std::vector<float>>& branch_outputs, std::vector<float>* output);

}  // namespace XER::Engine::Audio::FX