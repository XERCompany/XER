#pragma once

#include "../../models_builder/model_core/model.h"
#include <memory>

namespace XER::ModelsBuilder::Reader::Transform {

/// @brief Removes Identity and no-op layers (pass-through nodes)
class TransFuseIdentity {
 public:
  /// Remove all identity/pass-through layers from the model
  std::shared_ptr<Core::Model> Apply(std::shared_ptr<Core::Model> model);

 private:
  static bool IsIdentityLayer(const Core::Layer& layer);
};

}  // namespace XER::ModelsBuilder::Reader::Transform
