#include "layer_dropout.h"

namespace XER::ModelsBuilder::LayerFactory {

LayerDropout::LayerDropout(const std::string& name, float rate, bool training)
    : Core::Layer(Core::LayerType::Dropout), rate_(rate), training_(training) {
  SetLayerName(name);
}

std::shared_ptr<Core::Layer> MakeDropout(const std::string& name, float rate) {
  return std::make_shared<LayerDropout>(name, rate, false);
}

}  // namespace XER::ModelsBuilder::LayerFactory
