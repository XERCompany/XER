#include "custom_effect_core.h"

#include <algorithm>
#include <cmath>

#include "custom_effect_node.h"

namespace XER::Audio::FX {

bool ValidateCustomEffectPackage(const CustomEffectPackage& package, std::string* error_out) {
	if (package.nodes.empty()) {
		if (error_out != nullptr) {
			*error_out = "custom effect package has no nodes";
		}
		return false;
	}
	if (package.render_config.block_size == 0) {
		if (error_out != nullptr) {
			*error_out = "custom effect render block size must be positive";
		}
		return false;
	}
	for (const auto& node : package.nodes) {
		if (!ValidateCustomEffectNode(node, error_out)) {
			return false;
		}
	}
	return true;
}

float MixCustomEffectDryWet(float dry_sample, float wet_sample, float mix) {
	return dry_sample * (1.0f - mix) + wet_sample * mix;
}

void ComputeCustomEffectReportStats(const std::vector<float>& samples, CustomEffectReport* report) {
	if (report == nullptr || samples.empty()) {
		return;
	}
	double sum_squared = 0.0;
	for (float sample : samples) {
		report->peak = std::max(report->peak, std::abs(sample));
		sum_squared += static_cast<double>(sample) * static_cast<double>(sample);
	}
	report->rms = std::sqrt(sum_squared / static_cast<double>(samples.size()));
}

}  // namespace XER::Audio::FX
