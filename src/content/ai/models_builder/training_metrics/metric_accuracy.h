#pragma once

#include <cstddef>
#include <vector>

namespace XER::ModelsBuilder::TrainingMetrics {

class AccuracyMetric {
 public:
	void Reset();

	void Update(float prediction, float target, float threshold = 0.5f);
	void UpdateBatch(const std::vector<float>& predictions,
									 const std::vector<float>& targets,
									 float threshold = 0.5f);

	float Value() const;
	size_t CorrectCount() const { return correct_count_; }
	size_t SampleCount() const { return sample_count_; }

 private:
	size_t correct_count_ = 0U;
	size_t sample_count_ = 0U;
};

}  // namespace XER::ModelsBuilder::TrainingMetrics

