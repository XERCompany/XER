#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "custom_effect_flow.h"

namespace XER::Engine::Audio::FX {

CustomEffectPackage BuildSmokeCustomEffectPackage();
CustomEffectPackage BuildExampleCustomEffectPackage(const std::string& clap_plugin_reference = "builtin://gain");
CustomEffectPackage BuildNamedCustomEffectPackage(const std::string& effect_name, const std::string& clap_plugin_reference = "builtin://gain");
std::vector<std::string> ListAvailableCustomEffects();
bool RenderCustomEffectExample(
	float sample_rate,
	const std::vector<float>& input,
	std::vector<float>* output,
	CustomEffectReport* report_out,
	std::string* error_out = nullptr);

bool RunCustomEffectPackage(
	const CustomEffectPackage& package,
	float sample_rate,
	const std::vector<float>& input,
	std::vector<float>* output,
	CustomEffectReport* report_out,
	std::string* error_out = nullptr);

bool RunCustomEffectPackageInterleaved(
	const CustomEffectPackage& package,
	float sample_rate,
	const std::vector<float>& input,
	int channels,
	std::vector<float>* output,
	CustomEffectReport* report_out,
	std::string* error_out = nullptr);

}  // namespace XER::Engine::Audio::FX
