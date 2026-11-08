#pragma once

#include "../model_core/model.h"

#include <memory>

namespace XER::ModelsBuilder::Operations {

class ModelMerger {
 public:
  std::shared_ptr<Core::Model> MergeSequential(const Core::Model& first,
											   const Core::Model& second,
											   const std::string& merged_name = "merged_model") const;
};

}  // namespace XER::ModelsBuilder::Operations

