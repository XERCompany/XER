#pragma once

#include <string>

#include "audio/effects_rack/custom_effect_struct.h"

namespace XER::Engine::Audio::FX::Customs {

std::string NormalizeFxCustomPresetName(std::string name);
bool BuildFxCustomEffectPackage(
	const std::string& effect_name,
	const std::string& clap_plugin_reference,
	CustomEffectPackage* package_out);

}  // namespace XER::Engine::Audio::FX::Customs
