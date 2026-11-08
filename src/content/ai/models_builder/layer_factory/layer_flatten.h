#pragma once

#include "../model_core/layer.h"
#include <memory>
#include <string>

namespace XER::ModelsBuilder::LayerFactory {

/// @brief Flatten layer: collapse spatial dims into 1D
class LayerFlatten : public Core::Layer {
 public:
  explicit LayerFlatten(const std::string& name,
                         int start_dim = 1, int end_dim = -1);
  int GetStartDim() const { return start_dim_; }
  int GetEndDim()   const { return end_dim_; }

 private:
  int start_dim_, end_dim_;
};

std::shared_ptr<Core::Layer> MakeFlatten(const std::string& name,
                                           int start_dim = 1,
                                           int end_dim   = -1);

}  // namespace XER::ModelsBuilder::LayerFactory
