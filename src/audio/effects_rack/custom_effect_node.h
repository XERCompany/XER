#pragma once

#include <cstddef>
#include <string>

#include "custom_effect_struct.h"

namespace XER::Audio::FX {

bool ValidateCustomEffectNode(const CustomEffectNode& node, std::string* error_out = nullptr);
float ResolveCustomEffectNodeMix(const CustomEffectNode& node, size_t frame_index);

}  // namespace XER::Audio::FX
