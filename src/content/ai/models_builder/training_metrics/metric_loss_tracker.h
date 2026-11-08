#pragma once

#include <cstddef>
#include <vector>

namespace XER::ModelsBuilder::TrainingMetrics {

class LossTracker {
 public:
	void Reset();
	void AddLoss(float loss_value);

	float Latest() const;
	float Best() const;
	float Mean() const;
	float MovingAverage(size_t window) const;

	const std::vector<float>& History() const { return losses_; }
	size_t Count() const { return losses_.size(); }

 private:
	std::vector<float> losses_;
};

}  // namespace XER::ModelsBuilder::TrainingMetrics

