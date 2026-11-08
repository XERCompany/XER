#pragma once

#include "../model_core/model.h"

#include <memory>
#include <string>

namespace XER::ModelsBuilder::Operations {

class ModelUpdater {
 public:
  std::shared_ptr<Core::Model> Rename(const Core::Model& model, const std::string& new_name) const;
  std::shared_ptr<Core::Model> ReplaceLayer(const Core::Model& model,
											size_t index,
											std::shared_ptr<Core::Layer> layer) const;
  std::shared_ptr<Core::Model> AppendLayer(const Core::Model& model,
										   std::shared_ptr<Core::Layer> layer) const;
};

}  // namespace XER::ModelsBuilder::Operations

