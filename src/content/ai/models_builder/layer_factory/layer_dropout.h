#pragma once

#include "../model_core/layer.h"
#include <memory>
#include <string>

namespace XER::ModelsBuilder::LayerFactory {

/// @brief Dropout regularization layer
class LayerDropout : public Core::Layer {
 public:
  explicit LayerDropout(const std::string& name, float rate = 0.5f,
                         bool training = false);
  float GetRate()     const { return rate_; }
  bool  IsTraining()  const { return training_; }

 private:
  float rate_;
  bool  training_;
};

std::shared_ptr<Core::Layer> MakeDropout(const std::string& name,
                                           float rate = 0.5f);

}  // namespace XER::ModelsBuilder::LayerFactory
