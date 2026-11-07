#pragma once

#include <string>

#include "custom_effect_package.h"

namespace XER::Engine::Audio::FX {

bool InvokeCustomEffectParameter(
	CustomEffectPackage* package,
	const std::string& node_label,
	const std::string& parameter_name,
	float value);

}  // namespace XER::Engine::Audio::FX
