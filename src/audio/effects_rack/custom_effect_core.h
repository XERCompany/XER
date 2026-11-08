#pragma once

#include <string>
#include <vector>

#include "custom_effect_struct.h"

namespace XER::Audio::FX {

bool ValidateCustomEffectPackage(const CustomEffectPackage& package, std::string* error_out = nullptr);
float MixCustomEffectDryWet(float dry_sample, float wet_sample, float mix);
void ComputeCustomEffectReportStats(const std::vector<float>& samples, CustomEffectReport* report);

}  // namespace XER::Audio::FX
