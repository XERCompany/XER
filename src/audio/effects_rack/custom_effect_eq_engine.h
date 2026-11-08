#pragma once

#include "custom_effect_struct.h"

namespace XER::Audio::FX {

CustomEffectNode CreateCustomEffectParametricEqNode(float frequency_hz, float q, float gain_db, float mix = 1.0f);

}  // namespace XER::Audio::FX
