#include "metric_loss_tracker.h"

#include <algorithm>
#include <numeric>

namespace XER::ModelsBuilder::TrainingMetrics {

void LossTracker::Reset() {
	losses_.clear();
}

void LossTracker::AddLoss(float loss_value) {
	losses_.push_back(loss_value);
}

float LossTracker::Latest() const {
	if (losses_.empty()) {
		return 0.0f;
	}
	return losses_.back();
}

float LossTracker::Best() const {
	if (losses_.empty()) {
		return 0.0f;
	}
	return *std::min_element(losses_.begin(), losses_.end());
}

float LossTracker::Mean() const {
	if (losses_.empty()) {
		return 0.0f;
	}
	const float sum = std::accumulate(losses_.begin(), losses_.end(), 0.0f);
	return sum / static_cast<float>(losses_.size());
}

float LossTracker::MovingAverage(size_t window) const {
	if (losses_.empty()) {
		return 0.0f;
	}

	const size_t effective_window = std::max<size_t>(1U, window);
	const size_t count = std::min(effective_window, losses_.size());
	const size_t start = losses_.size() - count;

	float sum = 0.0f;
	for (size_t i = start; i < losses_.size(); ++i) {
		sum += losses_[i];
	}
	return sum / static_cast<float>(count);
}

}  // namespace XER::ModelsBuilder::TrainingMetrics

