#pragma once

#include "../../models_builder/model_core/model.h"
#include <memory>

namespace XER::ModelsBuilder::Reader::Transform {

/// @brief Removes unreachable/dead layers from the model graph
class TransGraphCleaner {
 public:
  /// Remove null/invalid layers and compact the layer list
  std::shared_ptr<Core::Model> Apply(std::shared_ptr<Core::Model> model);
};

}  // namespace XER::ModelsBuilder::Reader::Transform
