#include "layer_activation.h"

namespace XER::ModelsBuilder::LayerFactory {

LayerActivation::LayerActivation(const std::string& name, ActType act,
                                   float alpha)
    : Core::Layer(Core::LayerType::Activation), act_(act), alpha_(alpha) {
  SetLayerName(name);
}

std::shared_ptr<Core::Layer> MakeActivation(
    const std::string& name, LayerActivation::ActType act, float alpha) {
  return std::make_shared<LayerActivation>(name, act, alpha);
}

}  // namespace XER::ModelsBuilder::LayerFactory
