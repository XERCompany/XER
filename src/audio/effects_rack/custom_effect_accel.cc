#include "custom_effect_accel.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#define XER_AUDIO_FX_HAS_SSE2 1
#include <emmintrin.h>
#else
#define XER_AUDIO_FX_HAS_SSE2 0
#endif

namespace XER::Engine::Audio::FX {
namespace {

bool EnvFlagEnabled(const char* name) {
	const char* value = std::getenv(name);
	if (value == nullptr) {
		return false;
	}
	return std::strcmp(value, "1") == 0 || std::strcmp(value, "true") == 0 || std::strcmp(value, "TRUE") == 0 || std::strcmp(value, "on") == 0;
}

bool EnvFlagDisabled(const char* name) {
	const char* value = std::getenv(name);
	if (value == nullptr) {
		return false;
	}
	return std::strcmp(value, "0") == 0 || std::strcmp(value, "false") == 0 || std::strcmp(value, "FALSE") == 0 || std::strcmp(value, "off") == 0;
}

bool IsCudaDriverPresent() {
#if defined(_WIN32)
	HMODULE module = LoadLibraryA("nvcuda.dll");
	if (module == nullptr) {
		return false;
	}
	FreeLibrary(module);
	return true;
#else
	void* module = dlopen("libcuda.so.1", RTLD_LAZY | RTLD_LOCAL);
	if (module == nullptr) {
		module = dlopen("libcuda.so", RTLD_LAZY | RTLD_LOCAL);
	}
	if (module == nullptr) {
		return false;
	}
	dlclose(module);
	return true;
#endif
}

}  // namespace

CustomEffectAccelerationInfo ResolveCustomEffectAcceleration() {
	CustomEffectAccelerationInfo info;
	info.simd_enabled = XER_AUDIO_FX_HAS_SSE2 && !EnvFlagDisabled("XER_AUDIO_FX_SIMD");
	info.cuda_requested = EnvFlagEnabled("XER_AUDIO_FX_CUDA");
	info.cuda_available = info.cuda_requested && IsCudaDriverPresent();
	if (info.cuda_available) {
		info.backend_tag = info.simd_enabled ? "cuda-ready+cpu-simd" : "cuda-ready+cpu-scalar";
	} else if (info.cuda_requested) {
		info.backend_tag = info.simd_enabled ? "cuda-unavailable+cpu-simd" : "cuda-unavailable+cpu-scalar";
	} else {
		info.backend_tag = info.simd_enabled ? "cpu-simd" : "cpu-scalar";
	}
	return info;
}

bool ResolveCustomEffectNodeConstantMix(const CustomEffectNode& node, float* mix_out) {
	if (mix_out == nullptr) {
		return false;
	}
	if (node.mix_curve.empty()) {
		*mix_out = 1.0f;
		return true;
	}
	if (node.mix_curve.size() == 1u) {
		*mix_out = std::clamp(node.mix_curve.front().value, 0.0f, 1.0f);
		return true;
	}
	return false;
}

void ApplyGainBuffer(const float* input, size_t frame_count, float gain, float* output) {
	if (input == nullptr || output == nullptr || frame_count == 0u) {
		return;
	}
	size_t index = 0;
#if XER_AUDIO_FX_HAS_SSE2
	const __m128 gain4 = _mm_set1_ps(gain);
	for (; index + 4u <= frame_count; index += 4u) {
		_mm_storeu_ps(output + index, _mm_mul_ps(_mm_loadu_ps(input + index), gain4));
	}
#endif
	for (; index < frame_count; ++index) {
		output[index] = input[index] * gain;
	}
}

void ApplyGainInPlace(std::vector<float>* samples, float gain) {
	if (samples == nullptr || samples->empty()) {
		return;
	}
	ApplyGainBuffer(samples->data(), samples->size(), gain, samples->data());
}

void AddScaledBuffer(const float* input, size_t frame_count, float scale, float* output) {
	if (input == nullptr || output == nullptr || frame_count == 0u) {
		return;
	}
	size_t index = 0;
#if XER_AUDIO_FX_HAS_SSE2
	const __m128 scale4 = _mm_set1_ps(scale);
	for (; index + 4u <= frame_count; index += 4u) {
		const __m128 sum = _mm_add_ps(_mm_loadu_ps(output + index), _mm_mul_ps(_mm_loadu_ps(input + index), scale4));
		_mm_storeu_ps(output + index, sum);
	}
#endif
	for (; index < frame_count; ++index) {
		output[index] += input[index] * scale;
	}
}

void MixDryWetConstant(const float* dry, const float* wet, size_t frame_count, float mix, float* output) {
	if (dry == nullptr || wet == nullptr || output == nullptr || frame_count == 0u) {
		return;
	}
	mix = std::clamp(mix, 0.0f, 1.0f);
	const float dry_mix = 1.0f - mix;
	size_t index = 0;
#if XER_AUDIO_FX_HAS_SSE2
	const __m128 dry4 = _mm_set1_ps(dry_mix);
	const __m128 wet4 = _mm_set1_ps(mix);
	for (; index + 4u <= frame_count; index += 4u) {
		const __m128 mixed = _mm_add_ps(_mm_mul_ps(_mm_loadu_ps(dry + index), dry4), _mm_mul_ps(_mm_loadu_ps(wet + index), wet4));
		_mm_storeu_ps(output + index, mixed);
	}
#endif
	for (; index < frame_count; ++index) {
		output[index] = dry[index] * dry_mix + wet[index] * mix;
	}
}

void LimitBufferInPlace(std::vector<float>* samples, float ceiling) {
	if (samples == nullptr || samples->empty()) {
		return;
	}
	ceiling = std::max(0.001f, ceiling);
	float* data = samples->data();
	const size_t frame_count = samples->size();
	size_t index = 0;
#if XER_AUDIO_FX_HAS_SSE2
	const __m128 high = _mm_set1_ps(ceiling);
	const __m128 low = _mm_set1_ps(-ceiling);
	for (; index + 4u <= frame_count; index += 4u) {
		const __m128 clipped = _mm_min_ps(high, _mm_max_ps(low, _mm_loadu_ps(data + index)));
		_mm_storeu_ps(data + index, clipped);
	}
#endif
	for (; index < frame_count; ++index) {
		data[index] = std::clamp(data[index], -ceiling, ceiling);
	}
}

void SumNormalizeBuffers(const std::vector<std::vector<float>>& branch_outputs, std::vector<float>* output) {
	if (output == nullptr || branch_outputs.empty()) {
		return;
	}
	const size_t frame_count = branch_outputs.front().size();
	output->assign(frame_count, 0.0f);
	const float normalization = 1.0f / static_cast<float>(branch_outputs.size());
	for (const auto& branch_output : branch_outputs) {
		if (branch_output.size() != frame_count) {
			continue;
		}
		AddScaledBuffer(branch_output.data(), frame_count, normalization, output->data());
	}
}

}  // namespace XER::Engine::Audio::FX