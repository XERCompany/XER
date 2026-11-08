#include "model_splitter.h"

#include "model_cloner.h"

#include <algorithm>

namespace XER::ModelsBuilder::Operations {

std::pair<std::shared_ptr<Core::Model>, std::shared_ptr<Core::Model>> ModelSplitter::Split(
    const Core::Model& model,
    size_t split_layer_index) const {
  ModelCloner cloner;
  auto source = cloner.Clone(model, "");

  auto front = std::make_shared<Core::Model>(model.GetModelName() + "_front");
  auto back = std::make_shared<Core::Model>(model.GetModelName() + "_back");
  front->SetModelType(model.GetModelType());
  back->SetModelType(model.GetModelType());

  const size_t split = std::min(split_layer_index, source->GetLayerCount());
  for (size_t i = 0U; i < split; ++i) {
    auto layer = source->GetLayer(i);
    if (layer != nullptr) {
      front->AddLayer(layer);
    }
  }
  for (size_t i = split; i < source->GetLayerCount(); ++i) {
    auto layer = source->GetLayer(i);
    if (layer != nullptr) {
      back->AddLayer(layer);
    }
  }

  if (!model.GetInputShape().empty()) {
    front->Build(model.GetInputShape());
  }

  return {front, back};
}

}  // namespace XER::ModelsBuilder::Operations
