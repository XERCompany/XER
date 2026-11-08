#include "weight_validator.h"

#include "../utility/mb_error_handler.h"
#include "../utility/mb_logger.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <vector>

namespace XER::ModelsBuilder::Weights {

WeightValidationResult WeightValidator::ValidateTensor(const WeightTensor& tensor) const {
	WeightValidationResult result{false, tensor.values.size(), 0U, 0.0f, 0.0f, 0.0f};
	if (tensor.shape.empty() || tensor.values.empty()) {
		Utility::ModelBuilderErrorHandler::GetInstance().ReportError(
			Utility::ModelBuilderErrorCode::InvalidShape,
			"Weight tensor '" + tensor.name + "' is empty.",
			"ModelsBuilder::Weights::WeightValidator::ValidateTensor");
		return result;
	}

	const size_t expected_count = std::accumulate(
		tensor.shape.begin(), tensor.shape.end(), static_cast<size_t>(1), [](const size_t lhs, const uint32_t rhs) {
			return lhs * static_cast<size_t>(rhs);
		});
	if (expected_count != tensor.values.size()) {
		Utility::ModelBuilderErrorHandler::GetInstance().ReportError(
			Utility::ModelBuilderErrorCode::InvalidShape,
			"Weight tensor '" + tensor.name + "' has mismatched shape and storage size.",
			"ModelsBuilder::Weights::WeightValidator::ValidateTensor");
		return result;
	}

	auto [min_it, max_it] = std::minmax_element(tensor.values.begin(), tensor.values.end());
	result.min_value = *min_it;
	result.max_value = *max_it;
	result.mean_value = std::accumulate(tensor.values.begin(), tensor.values.end(), 0.0f) /
						static_cast<float>(tensor.values.size());
	result.zero_count = static_cast<size_t>(std::count_if(tensor.values.begin(), tensor.values.end(), [](const float value) {
		return std::fabs(value) <= 1.0e-8f;
	}));
	result.valid = std::isfinite(result.min_value) && std::isfinite(result.max_value) && std::isfinite(result.mean_value);

	if (!result.valid) {
		Utility::ModelBuilderErrorHandler::GetInstance().ReportError(
			Utility::ModelBuilderErrorCode::InvalidArgument,
			"Weight tensor '" + tensor.name + "' contains non-finite values.",
			"ModelsBuilder::Weights::WeightValidator::ValidateTensor");
	}

	return result;
}

bool WeightValidator::ValidatePack(const std::vector<WeightTensor>& tensors) const {
	bool all_valid = true;
	for (const WeightTensor& tensor : tensors) {
		const WeightValidationResult result = ValidateTensor(tensor);
		all_valid = all_valid && result.valid;
	}

	if (all_valid) {
		Utility::ModelBuilderLogger::GetInstance().Debug(
			"Validated weight pack with " + std::to_string(tensors.size()) + " tensors.");
	}
	return all_valid;
}

} // namespace XER::ModelsBuilder::Weights
