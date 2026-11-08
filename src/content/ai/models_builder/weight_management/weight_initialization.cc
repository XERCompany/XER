#include "weight_initialization.h"

#include "../utility/mb_error_handler.h"
#include "../utility/mb_logger.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <random>
#include <numeric>
#include <string>
#include <utility>
#include <vector>

namespace XER::ModelsBuilder::Weights {

namespace {

size_t ComputeElementCount(const std::vector<uint32_t>& shape) {
	if (shape.empty()) {
		return 0U;
	}

	return std::accumulate(shape.begin(), shape.end(), static_cast<size_t>(1), [](const size_t lhs, const uint32_t rhs) {
		return lhs * static_cast<size_t>(rhs);
	});
}

	std::mt19937& GlobalGenerator() {
		static std::mt19937 generator{std::random_device{}()};
	return generator;
}

} // namespace

WeightTensor WeightInitializer::InitializeTensor(const std::string& name,
												 const std::vector<uint32_t>& shape,
												 WeightInitializationScheme scheme) const {
	WeightTensor tensor{name, shape, {}};
	const size_t element_count = ComputeElementCount(shape);
	if (element_count == 0U) {
		Utility::ModelBuilderErrorHandler::GetInstance().ReportError(
			Utility::ModelBuilderErrorCode::InvalidShape,
			"Cannot initialize tensor with empty shape.",
			"ModelsBuilder::Weights::WeightInitializer::InitializeTensor");
		return tensor;
	}

	tensor.values.resize(element_count, 0.0f);
	const float fan_in = shape.empty() ? 1.0f : static_cast<float>(shape.front());
	const float fan_out = shape.size() > 1U ? static_cast<float>(shape.back()) : fan_in;
	const float xavier_limit = std::sqrt(6.0f / std::max(1.0f, fan_in + fan_out));
	const float he_stddev = std::sqrt(2.0f / std::max(1.0f, fan_in));

	switch (scheme) {
		case WeightInitializationScheme::Zeros:
			std::fill(tensor.values.begin(), tensor.values.end(), 0.0f);
			break;
		case WeightInitializationScheme::Ones:
			std::fill(tensor.values.begin(), tensor.values.end(), 1.0f);
			break;
		case WeightInitializationScheme::Uniform: {
				std::uniform_real_distribution<float> distribution(-0.05f, 0.05f);
			for (float& value : tensor.values) {
				value = distribution(GlobalGenerator());
			}
			break;
		}
		case WeightInitializationScheme::Normal: {
				std::normal_distribution<float> distribution(0.0f, 0.05f);
			for (float& value : tensor.values) {
				value = distribution(GlobalGenerator());
			}
			break;
		}
		case WeightInitializationScheme::XavierUniform: {
				std::uniform_real_distribution<float> distribution(-xavier_limit, xavier_limit);
			for (float& value : tensor.values) {
				value = distribution(GlobalGenerator());
			}
			break;
		}
		case WeightInitializationScheme::HeNormal: {
				std::normal_distribution<float> distribution(0.0f, he_stddev);
			for (float& value : tensor.values) {
				value = distribution(GlobalGenerator());
			}
			break;
		}
	}

	Utility::ModelBuilderLogger::GetInstance().Debug(
		"Initialized tensor '" + name + "' with " + std::to_string(element_count) + " elements.");
	return tensor;
}

std::vector<WeightTensor> WeightInitializer::InitializeDenseLayer(const Core::DenseLayer& layer,
																  const uint32_t input_units,
																  const WeightInitializationScheme scheme) const {
	std::vector<WeightTensor> tensors;
	if (input_units == 0U || layer.GetUnits() == 0U) {
		Utility::ModelBuilderErrorHandler::GetInstance().ReportError(
			Utility::ModelBuilderErrorCode::InvalidShape,
			"Dense layer weight initialization requires positive input and output units.",
			"ModelsBuilder::Weights::WeightInitializer::InitializeDenseLayer");
		return tensors;
	}

	tensors.push_back(InitializeTensor(layer.GetLayerName() + "/kernel", {input_units, layer.GetUnits()}, scheme));
	tensors.push_back(InitializeTensor(layer.GetLayerName() + "/bias", {layer.GetUnits()}, WeightInitializationScheme::Zeros));
	return tensors;
}

std::vector<WeightTensor> WeightInitializer::InitializeModel(const Core::Model& model,
															 const std::vector<uint32_t>& input_shape,
															 const WeightInitializationScheme scheme) const {
	std::vector<WeightTensor> tensors;
	if (input_shape.empty()) {
		Utility::ModelBuilderErrorHandler::GetInstance().ReportError(
			Utility::ModelBuilderErrorCode::InvalidShape,
			"Model initialization requires a non-empty input shape.",
			"ModelsBuilder::Weights::WeightInitializer::InitializeModel");
		return tensors;
	}

	uint32_t current_units = input_shape.back();
	for (const std::shared_ptr<Core::Layer>& layer : model.GetLayers()) {
		if (const std::shared_ptr<Core::DenseLayer> dense = std::dynamic_pointer_cast<Core::DenseLayer>(layer)) {
			std::vector<WeightTensor> layer_tensors = InitializeDenseLayer(*dense, current_units, scheme);
			tensors.insert(tensors.end(), layer_tensors.begin(), layer_tensors.end());
			current_units = dense->GetUnits();
		}
	}

	return tensors;
}

} // namespace XER::ModelsBuilder::Weights
