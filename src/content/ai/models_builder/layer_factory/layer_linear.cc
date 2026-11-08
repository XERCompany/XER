#include "layer_linear.h"

namespace XER::ModelsBuilder::LayerFactory {

LayerLinear::LayerLinear(const std::string& name, Config config)
    : Core::Layer(Core::LayerType::Dense), config_(config) {
  SetLayerName(name);
}

std::shared_ptr<Core::Layer> MakeLinear(const std::string& name,
                                          LayerLinear::Config config) {
  return std::make_shared<LayerLinear>(name, config);
}

}  // namespace XER::ModelsBuilder::LayerFactory
