#pragma once

#include "weight_initialization.h"

#include <string>
#include <vector>

namespace XER::ModelsBuilder::Weights {

class WeightLoader {
public:
	std::vector<WeightTensor> Load(const std::string& filepath) const;
};

} // namespace XER::ModelsBuilder::Weights
