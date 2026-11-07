#pragma once

#include <string>
#include <vector>

#include "custom_effect_struct.h"

namespace XER::Engine::Audio::FX {

std::vector<CustomEffectNode> CreateCustomEffectNodeList(const std::vector<std::string>& algorithm_names);

}  // namespace XER::Engine::Audio::FX
