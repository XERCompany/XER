#include "layer_attention.h"

namespace XER::ModelsBuilder::LayerFactory {

LayerAttention::LayerAttention(const std::string& name, Config config)
    : Core::Layer(Core::LayerType::Dense), config_(config) {
  SetLayerName(name);
}

std::shared_ptr<Core::Layer> MakeAttention(const std::string& name,
                                              LayerAttention::Config config) {
  return std::make_shared<LayerAttention>(name, config);
}

}  // namespace XER::ModelsBuilder::LayerFactory
