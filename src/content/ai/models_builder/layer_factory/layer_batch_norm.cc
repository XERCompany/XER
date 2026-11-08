#include "layer_batch_norm.h"

namespace XER::ModelsBuilder::LayerFactory {

LayerBatchNorm::LayerBatchNorm(const std::string& name, Config config)
    : Core::Layer(Core::LayerType::BatchNorm), config_(config) {
  SetLayerName(name);
}

std::shared_ptr<Core::Layer> MakeBatchNorm(const std::string& name,
                                              LayerBatchNorm::Config config) {
  return std::make_shared<LayerBatchNorm>(name, config);
}

}  // namespace XER::ModelsBuilder::LayerFactory
