#include "custom_effect_node.h"

#include <algorithm>

#include "custom_effect_node_curve_line.h"

namespace XER::Audio::FX {

bool ValidateCustomEffectNode(const CustomEffectNode& node, std::string* error_out) {
	if (node.label.empty()) {
		if (error_out != nullptr) {
			*error_out = "custom effect node label is empty";
		}
		return false;
	}
	if (node.backend == CustomEffectBackend::kBuiltinAlgorithm) {
		if (node.algorithm == CustomEffectAlgorithm::kUnknown || node.algorithm == CustomEffectAlgorithm::kPlugin) {
			if (error_out != nullptr) {
				*error_out = "builtin custom effect node has invalid algorithm: " + node.label;
			}
			return false;
		}
		return true;
	}
	if (node.backend == CustomEffectBackend::kClapPlugin) {
		if (node.algorithm != CustomEffectAlgorithm::kPlugin) {
			if (error_out != nullptr) {
				*error_out = "clap custom effect node must use plugin algorithm: " + node.label;
			}
			return false;
		}
		if (node.plugin_reference.empty()) {
			if (error_out != nullptr) {
				*error_out = "clap custom effect node plugin reference is empty: " + node.label;
			}
			return false;
		}
		return true;
	}
	if (error_out != nullptr) {
		*error_out = "custom effect node has unsupported backend: " + node.label;
	}
	return false;

	if (node.label.empty()) {
		if (error_out != nullptr) {
			*error_out = "custom effect node label is empty";
		}
		return false;
	}
}

float ResolveCustomEffectNodeMix(const CustomEffectNode& node, size_t frame_index) {
	return std::clamp(EvaluateCustomEffectCurve(node.mix_curve, frame_index, 1.0f), 0.0f, 1.0f);
}

}  // namespace XER::Audio::FX
