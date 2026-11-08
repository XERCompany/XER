#include "layer_embedding.h"

namespace XER::ModelsBuilder::LayerFactory {

LayerEmbedding::LayerEmbedding(const std::string& name, Config config)
    : Core::Layer(Core::LayerType::Dense), config_(config) {
  SetLayerName(name);
}

std::shared_ptr<Core::Layer> MakeEmbedding(const std::string& name,
                                              LayerEmbedding::Config config) {
  return std::make_shared<LayerEmbedding>(name, config);
}

}  // namespace XER::ModelsBuilder::LayerFactory
