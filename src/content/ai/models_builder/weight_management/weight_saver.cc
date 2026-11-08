#include "weight_saver.h"

#include "../utility/mb_error_handler.h"
#include "../utility/mb_logger.h"

#include <fstream>
#include <iomanip>
#include <string>
#include <vector>

namespace XER::ModelsBuilder::Weights {

bool WeightSaver::Save(const std::string& filepath, const std::vector<WeightTensor>& tensors) const {
	std::ofstream output(filepath, std::ios::binary | std::ios::trunc);
	if (!output.is_open()) {
		Utility::ModelBuilderErrorHandler::GetInstance().ReportError(
			Utility::ModelBuilderErrorCode::IoFailure,
			"Failed to open weight file for writing: " + filepath,
			"ModelsBuilder::Weights::WeightSaver::Save");
		return false;
	}

	output << tensors.size() << '\n';
	for (const WeightTensor& tensor : tensors) {
		output << tensor.name << '\n';
		output << tensor.shape.size();
		for (const uint32_t dimension : tensor.shape) {
			output << ' ' << dimension;
		}
		output << '\n';
		output << tensor.values.size();
		output << std::setprecision(9);
		for (const float value : tensor.values) {
			output << ' ' << value;
		}
		output << '\n';
	}

	Utility::ModelBuilderLogger::GetInstance().Info("Saved weights to " + filepath);
	return true;
}

} // namespace XER::ModelsBuilder::Weights
