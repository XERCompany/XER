#include "weight_analyzer.h"

#include "../utility/mb_logger.h"

#include <cmath>
#include <numeric>
#include <vector>

namespace XER::ModelsBuilder::Weights {

WeightStatistics WeightAnalyzer::AnalyzeTensor(const WeightTensor& tensor) const {
	float l1_norm = 0.0f;
	float l2_accumulator = 0.0f;
	size_t zero_count = 0U;
	float sum = 0.0f;
	for (const float value : tensor.values) {
		sum += value;
		l1_norm += std::fabs(value);
		l2_accumulator += value * value;
		if (std::fabs(value) <= 1.0e-8f) {
			++zero_count;
		}
	}

	WeightStatistics stats{};
	stats.count = tensor.values.size();
	stats.mean = tensor.values.empty() ? 0.0f : sum / static_cast<float>(tensor.values.size());
	if (!tensor.values.empty()) {
		float squared_error_sum = 0.0f;
		for (const float value : tensor.values) {
			const float delta = value - stats.mean;
			squared_error_sum += delta * delta;
		}
		stats.variance = squared_error_sum / static_cast<float>(tensor.values.size());
	} else {
		stats.variance = 0.0f;
	}
	stats.l1_norm = l1_norm;
	stats.l2_norm = std::sqrt(l2_accumulator);
	stats.sparsity_ratio = tensor.values.empty()
		? 0.0f
		: static_cast<float>(zero_count) / static_cast<float>(tensor.values.size());
	return stats;
}

WeightStatistics WeightAnalyzer::AnalyzePack(const std::vector<WeightTensor>& tensors) const {
	WeightTensor merged{"merged", {static_cast<uint32_t>(tensors.size())}, {}};
	for (const WeightTensor& tensor : tensors) {
		merged.values.insert(merged.values.end(), tensor.values.begin(), tensor.values.end());
	}

	WeightStatistics stats = AnalyzeTensor(merged);
	Utility::ModelBuilderLogger::GetInstance().Debug(
		"Analyzed weight pack with total elements " + std::to_string(stats.count));
	return stats;
}

} // namespace XER::ModelsBuilder::Weights
