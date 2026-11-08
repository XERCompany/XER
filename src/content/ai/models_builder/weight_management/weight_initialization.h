#pragma once

#include "../model_core/dense_layer.h"
#include "../model_core/model.h"

#include <cstdint>
#include <string>
#include <vector>

namespace XER::ModelsBuilder::Weights {

enum class WeightInitializationScheme : uint8_t {
	Zeros = 0,
	Ones = 1,
	Uniform = 2,
	Normal = 3,
	XavierUniform = 4,
	HeNormal = 5
};

struct WeightTensor {
	std::string name;
	std::vector<uint32_t> shape;
	std::vector<float> values;
};

class WeightInitializer {
public:
	WeightTensor InitializeTensor(const std::string& name,
								  const std::vector<uint32_t>& shape,
								  WeightInitializationScheme scheme) const;

	std::vector<WeightTensor> InitializeDenseLayer(const Core::DenseLayer& layer,
												   uint32_t input_units,
												   WeightInitializationScheme scheme) const;

	std::vector<WeightTensor> InitializeModel(const Core::Model& model,
											  const std::vector<uint32_t>& input_shape,
											  WeightInitializationScheme scheme) const;
};

} // namespace XER::ModelsBuilder::Weights
