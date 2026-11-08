#include "layer_conv3d.h"

namespace XER::ModelsBuilder::LayerFactory {

LayerConv3D::LayerConv3D(const std::string& name, Config config)
    : Core::Layer(Core::LayerType::Conv2D), config_(config) {
  SetLayerName(name);
}

std::shared_ptr<Core::Layer> MakeConv3D(const std::string& name,
                                         LayerConv3D::Config config) {
  return std::make_shared<LayerConv3D>(name, config);
}

}  // namespace XER::ModelsBuilder::LayerFactory
