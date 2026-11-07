#pragma once

#include <string>

#include "audio/effects_rack/custom_effect_struct.h"

namespace XER::Engine::Audio::FX::Customs {

bool BuildFxCustomVintageEffectPackage(const std::string& normalized_name, CustomEffectPackage* package_out);

}  // namespace XER::Engine::Audio::FX::Customs
