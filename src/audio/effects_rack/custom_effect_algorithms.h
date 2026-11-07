#pragma once

#include <string>
#include <vector>

#include "custom_effect_struct.h"

namespace XER::Engine::Audio::FX {

std::vector<std::string> ListCustomEffectAlgorithms();
CustomEffectNode CreateDefaultCustomEffectNode(const std::string& algorithm_name);
std::vector<std::string> ListCustomEffectPresets();

}  // namespace XER::Engine::Audio::FX
