#include "weight_loader.h"

#include "../utility/mb_error_handler.h"
#include "../utility/mb_logger.h"

#include <fstream>
#include <limits>
#include <string>
#include <vector>

namespace XER::ModelsBuilder::Weights {

std::vector<WeightTensor> WeightLoader::Load(const std::string& filepath) const {
	std::ifstream input(filepath, std::ios::binary);
	if (!input.is_open()) {
		Utility::ModelBuilderErrorHandler::GetInstance().ReportError(
			Utility::ModelBuilderErrorCode::IoFailure,
			"Failed to open weight file for reading: " + filepath,
			"ModelsBuilder::Weights::WeightLoader::Load");
		return {};
	}

	std::vector<WeightTensor> tensors;
	size_t tensor_count = 0U;
	input >> tensor_count;
	input.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
	tensors.reserve(tensor_count);

	for (size_t tensor_index = 0; tensor_index < tensor_count; ++tensor_index) {
		WeightTensor tensor{};
		std::getline(input, tensor.name);

		size_t shape_size = 0U;
		input >> shape_size;
		tensor.shape.resize(shape_size, 0U);
		for (size_t shape_index = 0; shape_index < shape_size; ++shape_index) {
			input >> tensor.shape[shape_index];
		}
		input.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

		size_t value_count = 0U;
		input >> value_count;
		tensor.values.resize(value_count, 0.0f);
		for (size_t value_index = 0; value_index < value_count; ++value_index) {
			input >> tensor.values[value_index];
		}
		input.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
		tensors.push_back(std::move(tensor));
	}

	Utility::ModelBuilderLogger::GetInstance().Info("Loaded weights from " + filepath);
	return tensors;
}

} // namespace XER::ModelsBuilder::Weights
