#pragma once

#include "../model_core/model.h"

#include <memory>
#include <string>

namespace XER::ModelsBuilder::Operations {

class ModelCompressor {
 public:
	bool CompressModelToFile(const Core::Model& model, const std::string& output_path) const;
	std::string CompressModelToString(const Core::Model& model) const;
	std::shared_ptr<Core::Model> DecompressModel(const std::string& compressed) const;
	std::shared_ptr<Core::Model> DecompressModelFromFile(const std::string& input_path) const;
};

}  // namespace XER::ModelsBuilder::Operations

