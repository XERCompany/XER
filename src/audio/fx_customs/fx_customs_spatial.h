#pragma once

#include <string>
#include <vector>

#include "audio/effects_rack/custom_effect_struct.h"

namespace XER::Engine::Audio::FX::Customs {

bool IsFxCustomSpatialPreset(const std::string& normalized_name);
void ApplyFxCustomSpatialVariant(
	CustomEffectPackage* package,
	const std::string& normalized_name,
	int channel_index,
	int channel_count);
bool ApplyFxCustomSharedSpatialBus(
	const std::string& normalized_name,
	float sample_rate,
	std::vector<std::vector<float>>* channels,
	CustomEffectReport* aggregate_report,
	std::string* error_out);

}  // namespace XER::Engine::Audio::FX::Customs
