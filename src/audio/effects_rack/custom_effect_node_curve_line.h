#pragma once

#include <cstddef>
#include <vector>

#include "custom_effect_struct.h"

namespace XER::Audio::FX {

float EvaluateCustomEffectCurve(
	const std::vector<CustomEffectCurvePoint>& points,
	size_t frame_index,
	float default_value);

}  // namespace XER::Audio::FX
