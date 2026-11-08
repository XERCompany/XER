#include "layer_conv2d.h"

namespace XER::ModelsBuilder::LayerFactory {

LayerConv2D::LayerConv2D(const std::string& name, Config config)
    : Core::Layer(Core::LayerType::Conv2D), config_(config) {
  SetLayerName(name);
}

std::shared_ptr<Core::Layer> MakeConv2D(const std::string& name,
                                         LayerConv2D::Config config) {
  return std::make_shared<LayerConv2D>(name, config);
}

}  // namespace XER::ModelsBuilder::LayerFactory
