#pragma once

#include <string>

#include "audio/effects_rack/custom_effect_struct.h"

namespace XER::Audio::FX::Customs {

bool BuildFxCustomSpatialEffectPackage(const std::string& normalized_name, CustomEffectPackage* package_out);

}  // namespace XER::Audio::FX::Customs
