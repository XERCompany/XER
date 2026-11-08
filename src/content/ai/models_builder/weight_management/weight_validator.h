#pragma once

#include "weight_initialization.h"

#include <cstddef>
#include <vector>

namespace XER::ModelsBuilder::Weights {

struct WeightValidationResult {
	bool valid;
	size_t element_count;
	size_t zero_count;
	float min_value;
	float max_value;
	float mean_value;
};

class WeightValidator {
public:
	WeightValidationResult ValidateTensor(const WeightTensor& tensor) const;
	bool ValidatePack(const std::vector<WeightTensor>& tensors) const;
};

} // namespace XER::ModelsBuilder::Weights
