#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "audio/effects_rack/fx_dynamics_compressor.h"
#include "audio/effects_rack/fx_eq_parametric.h"
#include "audio/effects_rack/fx_mod_chorus.h"
#include "plugin_parameter_bridge.h"

namespace XER::Audio::Plugin {

class BuiltinPluginHost {
public:
	bool Initialize(double sample_rate, uint32_t max_block_size);
	bool Load(const std::string& descriptor);
	bool Process(const float* input, float* output, uint32_t frames);
	bool SetParameter(uint32_t id, float value);
	bool GetParameter(uint32_t id, float* value) const;
	std::vector<PluginParameterInfo> GetParameters() const;
	std::string GetLoadedIdentifier() const;

private:
	enum class Kind {
		kNone,
		kPassthrough,
		kGain,
		kCompressor,
		kChorus,
		kParametricEq,
	};

	static bool ResolveDescriptor(const std::string& descriptor, Kind* kind_out, std::string* normalized_out);
	bool ApplyParameters();
	void RegisterPassthroughParameters();
	void RegisterGainParameters();
	void RegisterCompressorParameters();
	void RegisterChorusParameters();
	void RegisterParametricEqParameters();

	PluginParameterBridge parameter_bridge_;
	XER::Audio::FX::DynamicsCompressor compressor_;
	XER::Audio::FX::ModChorus chorus_;
	XER::Audio::FX::ParametricEQ parametric_eq_;
	double sample_rate_ = 44100.0;
	uint32_t max_block_size_ = 512;
	Kind kind_ = Kind::kNone;
	std::string loaded_identifier_;
	bool initialized_ = false;
	bool effect_initialized_ = false;
};

}  // namespace XER::Audio::Plugin