#pragma once

#include "../model_core/model.h"

#include <memory>
#include <string>

namespace XER::ModelsBuilder::Operations {

class ModelCloner {
 public:
  std::shared_ptr<Core::Model> Clone(const Core::Model& model,
									 const std::string& cloned_name_suffix = "_clone") const;
};

}  // namespace XER::ModelsBuilder::Operations

