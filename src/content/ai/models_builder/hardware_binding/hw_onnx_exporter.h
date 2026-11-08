#pragma once

#include "../model_core/model.h"

#include <string>

namespace XER::ModelsBuilder::HardwareBinding {

class OnnxExporter {
 public:
	bool IsAvailable() const;
	bool ExportModel(const Core::Model& model, const std::string& output_path) const;
	std::string BuildOnnxLikeText(const Core::Model& model) const;
};

}  // namespace XER::ModelsBuilder::HardwareBinding

