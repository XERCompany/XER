#include "plugin_builtin_host.h"
#include "helper/string_utils.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace XER::Audio::Plugin {

namespace {

constexpr uint32_t kGainParameterId = 0;
constexpr uint32_t kDynamicsThresholdParameterId = 0;
constexpr uint32_t kDynamicsRatioParameterId = 1;
constexpr uint32_t kChorusRateParameterId = 0;
constexpr uint32_t kChorusDepthParameterId = 1;
constexpr uint32_t kChorusMixParameterId = 2;
constexpr uint32_t kEqFrequencyParameterId = 0;
constexpr uint32_t kEqQParameterId = 1;
constexpr uint32_t kEqGainParameterId = 2;

float DbToLinear(float db_value) {
	return std::pow(10.0f, db_value / 20.0f);
}

std::string NormalizeDescriptor(std::string descriptor) {
	descriptor = XER::Helper::String::ToLowerAscii(descriptor);
	constexpr const char* kBuiltinPrefix = "builtin://";
	if (descriptor.rfind(kBuiltinPrefix, 0) == 0) {
		descriptor.erase(0, std::strlen(kBuiltinPrefix));
	}
	constexpr const char* kBuiltinShortPrefix = "builtin:";
	if (descriptor.rfind(kBuiltinShortPrefix, 0) == 0) {
		descriptor.erase(0, std::strlen(kBuiltinShortPrefix));
	}
	return descriptor;
}

}  // namespace

bool BuiltinPluginHost::Initialize(double sample_rate, uint32_t max_block_size) {
	if (sample_rate <= 0.0 || max_block_size == 0) {
		return false;
	}
	sample_rate_ = sample_rate;
	max_block_size_ = max_block_size;
	initialized_ = true;
	effect_initialized_ = false;
	return true;
}

bool BuiltinPluginHost::ResolveDescriptor(const std::string& descriptor, Kind* kind_out, std::string* normalized_out) {
	if (kind_out == nullptr || normalized_out == nullptr) {
		return false;
	}
	const std::string normalized = NormalizeDescriptor(descriptor);
	if (normalized == "passthrough") {
		*kind_out = Kind::kPassthrough;
	} else if (normalized == "gain") {
		*kind_out = Kind::kGain;
	} else if (normalized == "compressor") {
		*kind_out = Kind::kCompressor;
	} else if (normalized == "chorus") {
		*kind_out = Kind::kChorus;
	} else if (normalized == "parametric_eq" || normalized == "eq") {
		*kind_out = Kind::kParametricEq;
	} else {
		return false;
	}
	*normalized_out = normalized;
	return true;
}

bool BuiltinPluginHost::Load(const std::string& descriptor) {
	if (!initialized_) {
		return false;
	}

	Kind resolved_kind = Kind::kNone;
	std::string normalized;
	if (!ResolveDescriptor(descriptor, &resolved_kind, &normalized)) {
		return false;
	}

	kind_ = resolved_kind;
	loaded_identifier_ = "builtin://" + normalized;
	parameter_bridge_.Clear();
	effect_initialized_ = false;

	switch (kind_) {
	case Kind::kPassthrough:
		RegisterPassthroughParameters();
		break;
	case Kind::kGain:
		RegisterGainParameters();
		break;
	case Kind::kCompressor:
		RegisterCompressorParameters();
		break;
	case Kind::kChorus:
		RegisterChorusParameters();
		effect_initialized_ = chorus_.Initialize(static_cast<float>(sample_rate_), 4096);
		break;
	case Kind::kParametricEq:
		RegisterParametricEqParameters();
		effect_initialized_ = parametric_eq_.Initialize(static_cast<float>(sample_rate_), 1);
		break;
	case Kind::kNone:
		return false;
	}

	if ((kind_ == Kind::kChorus || kind_ == Kind::kParametricEq) && !effect_initialized_) {
		return false;
	}
	return ApplyParameters();
}

bool BuiltinPluginHost::SetParameter(uint32_t id, float value) {
	if (!parameter_bridge_.SetValue(id, value)) {
		return false;
	}
	return ApplyParameters();
}

bool BuiltinPluginHost::GetParameter(uint32_t id, float* value) const {
	if (value == nullptr) {
		return false;
	}
	return parameter_bridge_.GetValue(id, *value);
}

std::vector<PluginParameterInfo> BuiltinPluginHost::GetParameters() const {
	return parameter_bridge_.Snapshot();
}

std::string BuiltinPluginHost::GetLoadedIdentifier() const {
	return loaded_identifier_;
}

bool BuiltinPluginHost::Process(const float* input, float* output, uint32_t frames) {
	if (!initialized_ || kind_ == Kind::kNone || input == nullptr || output == nullptr || frames == 0 || frames > max_block_size_) {
		return false;
	}

	switch (kind_) {
	case Kind::kPassthrough:
		std::copy(input, input + frames, output);
		return true;
	case Kind::kGain: {
		float gain_db = 0.0f;
		if (!parameter_bridge_.GetValue(kGainParameterId, gain_db)) {
			return false;
		}
		const float linear = DbToLinear(gain_db);
		for (uint32_t i = 0; i < frames; ++i) {
			output[i] = input[i] * linear;
		}
		return true;
	}
	case Kind::kCompressor:
		return compressor_.ProcessBlock(input, frames, output);
	case Kind::kChorus:
		return chorus_.ProcessBlock(input, frames, output);
	case Kind::kParametricEq:
		return parametric_eq_.ProcessBlock(input, frames, output);
	case Kind::kNone:
		return false;
	}
	return false;
}

bool BuiltinPluginHost::ApplyParameters() {
	switch (kind_) {
	case Kind::kPassthrough:
	case Kind::kGain:
		return true;
	case Kind::kCompressor: {
		float threshold_db = -18.0f;
		float ratio = 4.0f;
		return parameter_bridge_.GetValue(kDynamicsThresholdParameterId, threshold_db)
			&& parameter_bridge_.GetValue(kDynamicsRatioParameterId, ratio)
			&& (compressor_.SetThresholdDb(threshold_db), compressor_.SetRatio(ratio), true);
	}
	case Kind::kChorus: {
		float rate_hz = 0.6f;
		float depth_samples = 18.0f;
		float mix = 0.35f;
		return parameter_bridge_.GetValue(kChorusRateParameterId, rate_hz)
			&& parameter_bridge_.GetValue(kChorusDepthParameterId, depth_samples)
			&& parameter_bridge_.GetValue(kChorusMixParameterId, mix)
			&& (chorus_.SetRateHz(rate_hz), chorus_.SetDepthSamples(depth_samples), chorus_.SetMix(mix), true);
	}
	case Kind::kParametricEq: {
		float frequency = 1000.0f;
		float q = 1.0f;
		float gain_db = 0.0f;
		if (!parameter_bridge_.GetValue(kEqFrequencyParameterId, frequency)
			|| !parameter_bridge_.GetValue(kEqQParameterId, q)
			|| !parameter_bridge_.GetValue(kEqGainParameterId, gain_db)) {
			return false;
		}
		return parametric_eq_.SetBand(0, XER::Audio::FX::ParametricBand{frequency, q, gain_db, true});
	}
	case Kind::kNone:
		return false;
	}
	return false;
}

void BuiltinPluginHost::RegisterPassthroughParameters() {}

void BuiltinPluginHost::RegisterGainParameters() {
	parameter_bridge_.RegisterParameter(kGainParameterId, "gain_db", 0.0f, -24.0f, 24.0f);
}

void BuiltinPluginHost::RegisterCompressorParameters() {
	parameter_bridge_.RegisterParameter(kDynamicsThresholdParameterId, "threshold_db", -18.0f, -48.0f, 0.0f);
	parameter_bridge_.RegisterParameter(kDynamicsRatioParameterId, "ratio", 4.0f, 1.0f, 20.0f);
}

void BuiltinPluginHost::RegisterChorusParameters() {
	parameter_bridge_.RegisterParameter(kChorusRateParameterId, "rate_hz", 0.6f, 0.05f, 5.0f);
	parameter_bridge_.RegisterParameter(kChorusDepthParameterId, "depth_samples", 18.0f, 1.0f, 128.0f);
	parameter_bridge_.RegisterParameter(kChorusMixParameterId, "mix", 0.35f, 0.0f, 1.0f);
}

void BuiltinPluginHost::RegisterParametricEqParameters() {
	parameter_bridge_.RegisterParameter(kEqFrequencyParameterId, "frequency_hz", 1000.0f, 50.0f, 12000.0f);
	parameter_bridge_.RegisterParameter(kEqQParameterId, "q", 1.0f, 0.1f, 10.0f);
	parameter_bridge_.RegisterParameter(kEqGainParameterId, "gain_db", 0.0f, -18.0f, 18.0f);
}

}  // namespace XER::Audio::Plugin