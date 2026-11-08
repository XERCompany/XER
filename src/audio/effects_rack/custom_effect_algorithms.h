#pragma once

#include <string>
#include <vector>

#include "custom_effect_struct.h"

namespace XER::Audio::FX {

std::vector<std::string> ListCustomEffectAlgorithms();
CustomEffectNode CreateDefaultCustomEffectNode(const std::string& algorithm_name);
std::vector<std::string> ListCustomEffectPresets();

}  // namespace XER::Audio::FX
