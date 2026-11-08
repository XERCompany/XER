#include "model_pruner.h"

#include "model_cloner.h"

#include <algorithm>

namespace XER::ModelsBuilder::Operations {

std::shared_ptr<Core::Model> ModelPruner::KeepFirstLayers(const Core::Model& model,
        size_t layers_to_keep) const {
	ModelCloner cloner;
	auto source = cloner.Clone(model, "");
	auto pruned = std::make_shared<Core::Model>(model.GetModelName() + "_pruned");
	pruned->SetModelType(model.GetModelType());

	const size_t keep = std::min(layers_to_keep, source->GetLayerCount());
	for (size_t i = 0U; i < keep; ++i) {
		auto layer = source->GetLayer(i);
		if (layer != nullptr) {
			pruned->AddLayer(layer);
		}
	}

	if (!model.GetInputShape().empty()) {
		if (pruned->Build(model.GetInputShape()) && model.IsCompiled()) {
			pruned->Compile();
		}
	}

	return pruned;
}

std::shared_ptr<Core::Model> ModelPruner::PruneLastLayers(const Core::Model& model,
																													size_t layers_to_prune) const {
	if (layers_to_prune >= model.GetLayerCount()) {
		return KeepFirstLayers(model, 0U);
	}
	return KeepFirstLayers(model, model.GetLayerCount() - layers_to_prune);
}

}  // namespace XER::ModelsBuilder::Operations

