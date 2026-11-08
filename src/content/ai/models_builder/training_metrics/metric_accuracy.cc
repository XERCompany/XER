#include "metric_accuracy.h"

#include <algorithm>

namespace XER::ModelsBuilder::TrainingMetrics {

void AccuracyMetric::Reset() {
	correct_count_ = 0U;
	sample_count_ = 0U;
}

void AccuracyMetric::Update(float prediction, float target, float threshold) {
	const float clamped_threshold = std::clamp(threshold, 0.0f, 1.0f);
	const bool predicted_positive = prediction >= clamped_threshold;
	const bool target_positive = target >= clamped_threshold;
	if (predicted_positive == target_positive) {
		++correct_count_;
	}
	++sample_count_;
}

void AccuracyMetric::UpdateBatch(const std::vector<float>& predictions,
																 const std::vector<float>& targets,
																 float threshold) {
	const size_t count = std::min(predictions.size(), targets.size());
	for (size_t i = 0; i < count; ++i) {
		Update(predictions[i], targets[i], threshold);
	}
}

float AccuracyMetric::Value() const {
	if (sample_count_ == 0U) {
		return 0.0f;
	}
	return static_cast<float>(correct_count_) / static_cast<float>(sample_count_);
}

}  // namespace XER::ModelsBuilder::TrainingMetrics

