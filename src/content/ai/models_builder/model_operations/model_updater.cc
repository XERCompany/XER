#include "model_updater.h"

#include "model_cloner.h"

namespace XER::ModelsBuilder::Operations {

std::shared_ptr<Core::Model> ModelUpdater::Rename(const Core::Model& model,
                                                  const std::string& new_name) const {
  ModelCloner cloner;
  auto updated = cloner.Clone(model, "");
  updated->SetModelName(new_name);
  return updated;
}

std::shared_ptr<Core::Model> ModelUpdater::ReplaceLayer(const Core::Model& model,
                                                        size_t index,
                                                        std::shared_ptr<Core::Layer> layer) const {
  ModelCloner cloner;
  auto source = cloner.Clone(model, "");
  auto updated = std::make_shared<Core::Model>(model.GetModelName() + "_updated");
  updated->SetModelType(model.GetModelType());

  for (size_t i = 0U; i < source->GetLayerCount(); ++i) {
    if (i == index && layer != nullptr) {
      updated->AddLayer(layer);
      continue;
    }
    auto current = source->GetLayer(i);
    if (current != nullptr) {
      updated->AddLayer(current);
    }
  }

  if (!model.GetInputShape().empty()) {
    if (updated->Build(model.GetInputShape()) && model.IsCompiled()) {
      updated->Compile();
    }
  }

  return updated;
}

std::shared_ptr<Core::Model> ModelUpdater::AppendLayer(const Core::Model& model,
                                                       std::shared_ptr<Core::Layer> layer) const {
  ModelCloner cloner;
  auto updated = cloner.Clone(model, "_extended");
  if (layer != nullptr) {
    updated->AddLayer(layer);
    if (!model.GetInputShape().empty()) {
      if (updated->Build(model.GetInputShape()) && model.IsCompiled()) {
        updated->Compile();
      }
    }
  }
  return updated;
}

}  // namespace XER::ModelsBuilder::Operations
