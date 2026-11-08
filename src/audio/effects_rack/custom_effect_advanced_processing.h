#pragma once

#include <string>
#include <vector>

#include "custom_effect_struct.h"

namespace XER::Audio::FX {

bool TryRunAdvancedCustomEffectPackage(
	const CustomEffectPackage& package,
	float sample_rate,
	const std::vector<float>& input,
	std::vector<float>* output,
	CustomEffectReport* report_out,
	bool* handled_out,
	std::string* error_out = nullptr);

}  // namespace XER::Audio::FX