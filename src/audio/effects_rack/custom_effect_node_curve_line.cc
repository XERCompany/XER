#include "custom_effect_node_curve_line.h"

namespace XER::Audio::FX {

float EvaluateCustomEffectCurve(
	const std::vector<CustomEffectCurvePoint>& points,
	size_t frame_index,
	float default_value) {
	if (points.empty()) {
		return default_value;
	}
	if (frame_index <= points.front().frame_index) {
		return points.front().value;
	}
	for (size_t index = 1; index < points.size(); ++index) {
		const CustomEffectCurvePoint& previous = points[index - 1];
		const CustomEffectCurvePoint& current = points[index];
		if (frame_index > current.frame_index) {
			continue;
		}
		const size_t span = current.frame_index - previous.frame_index;
		if (span == 0) {
			return current.value;
		}
		const float t = static_cast<float>(frame_index - previous.frame_index) / static_cast<float>(span);
		return previous.value + (current.value - previous.value) * t;
	}
	return points.back().value;
}

}  // namespace XER::Audio::FX
