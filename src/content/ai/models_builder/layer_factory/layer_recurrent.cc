#include "layer_recurrent.h"

namespace XER::ModelsBuilder::LayerFactory {

LayerRecurrent::LayerRecurrent(const std::string& name, Config config)
    : Core::Layer(Core::LayerType::LSTM), config_(config) {
  SetLayerName(name);
}

std::shared_ptr<Core::Layer> MakeRecurrent(const std::string& name,
                                              LayerRecurrent::Config config) {
  return std::make_shared<LayerRecurrent>(name, config);
}

}  // namespace XER::ModelsBuilder::LayerFactory
