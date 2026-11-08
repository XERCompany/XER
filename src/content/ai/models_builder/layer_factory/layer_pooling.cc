#include "layer_pooling.h"

namespace XER::ModelsBuilder::LayerFactory {

LayerPooling::LayerPooling(const std::string& name, Config config)
    : Core::Layer(Core::LayerType::MaxPool), config_(config) {
  SetLayerName(name);
}

std::shared_ptr<Core::Layer> MakePooling(const std::string& name,
                                          LayerPooling::Config config) {
  return std::make_shared<LayerPooling>(name, config);
}

}  // namespace XER::ModelsBuilder::LayerFactory
