#pragma once

#include "weight_initialization.h"

#include <string>
#include <vector>

namespace XER::ModelsBuilder::Weights {

class WeightSaver {
public:
	bool Save(const std::string& filepath, const std::vector<WeightTensor>& tensors) const;
};

} // namespace XER::ModelsBuilder::Weights
