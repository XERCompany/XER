#pragma once

#include "weight_initialization.h"

#include <cstddef>
#include <vector>

namespace XER::ModelsBuilder::Weights {

struct WeightStatistics {
	size_t count;
	float mean;
	float variance;
	float l1_norm;
	float l2_norm;
	float sparsity_ratio;
};

class WeightAnalyzer {
public:
	WeightStatistics AnalyzeTensor(const WeightTensor& tensor) const;
	WeightStatistics AnalyzePack(const std::vector<WeightTensor>& tensors) const;
};

} // namespace XER::ModelsBuilder::Weights
