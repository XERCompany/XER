#include "metric_grad_norm.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace XER::ModelsBuilder::TrainingMetrics {

void GradientNormMetric::Reset() {
	norms_.clear();
}

void GradientNormMetric::AddNorm(float norm_value) {
	norms_.push_back(norm_value);
}

void GradientNormMetric::AddGradient(const std::vector<float>& gradient_values) {
	float l2_sum = 0.0f;
	for (float value : gradient_values) {
		l2_sum += value * value;
	}
	AddNorm(std::sqrt(l2_sum));
}

float GradientNormMetric::Latest() const {
	if (norms_.empty()) {
		return 0.0f;
	}
	return norms_.back();
}

float GradientNormMetric::Max() const {
	if (norms_.empty()) {
		return 0.0f;
	}
	return *std::max_element(norms_.begin(), norms_.end());
}

float GradientNormMetric::Mean() const {
	if (norms_.empty()) {
		return 0.0f;
	}
	const float sum = std::accumulate(norms_.begin(), norms_.end(), 0.0f);
	return sum / static_cast<float>(norms_.size());
}

}  // namespace XER::ModelsBuilder::TrainingMetrics

