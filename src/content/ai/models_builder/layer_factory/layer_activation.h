#pragma once

#include "../model_core/layer.h"
#include <memory>
#include <string>

namespace XER::ModelsBuilder::LayerFactory {

/// @brief Activation layer (Relu, Gelu, Sigmoid, Tanh, etc.)
class LayerActivation : public Core::Layer {
 public:
  enum class ActType { Relu, Gelu, Sigmoid, Tanh, LeakyRelu, Silu, Softmax };

  explicit LayerActivation(const std::string& name, ActType act,
                             float alpha = 0.01f);
  ActType GetActType() const { return act_; }
  float   GetAlpha()   const { return alpha_; }

 private:
  ActType act_;
  float   alpha_;
};

std::shared_ptr<Core::Layer> MakeActivation(
    const std::string& name, LayerActivation::ActType act,
    float alpha = 0.01f);

}  // namespace XER::ModelsBuilder::LayerFactory
