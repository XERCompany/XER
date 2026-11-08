#pragma once

#include "../model_core/model.h"

#include <memory>

namespace XER::ModelsBuilder::Operations {

class ModelFuser {
 public:
	std::shared_ptr<Core::Model> Fuse(const Core::Model& model) const;
};

}  // namespace XER::ModelsBuilder::Operations

