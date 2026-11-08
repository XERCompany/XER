#pragma once

#include <string>
#include <vector>

#include "custom_effect_struct.h"

namespace XER::Audio::FX {

float GetCustomEffectParameterValue(
	const std::vector<CustomEffectParameter>& parameters,
	const std::string& name,
	float default_value);

void SetCustomEffectParameterValue(
	std::vector<CustomEffectParameter>* parameters,
	const std::string& name,
	float value);

}  // namespace XER::Audio::FX
