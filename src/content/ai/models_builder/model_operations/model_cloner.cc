#include "model_cloner.h"

#include "../model_core/dense_layer.h"

namespace XER::ModelsBuilder::Operations {

namespace {

std::shared_ptr<Core::Layer> CloneLayer(const std::shared_ptr<Core::Layer>& layer) {
	if (layer == nullptr) {
		return nullptr;
	}

	if (const auto dense = std::dynamic_pointer_cast<Core::DenseLayer>(layer)) {
		auto copy = std::make_shared<Core::DenseLayer>(dense->GetUnits());
		copy->SetLayerName(dense->GetLayerName());
		copy->SetActivation(dense->GetActivation());
		return copy;
	}

	return layer;
}

}  // namespace

std::shared_ptr<Core::Model> ModelCloner::Clone(const Core::Model& model, const std::string& cloned_name_suffix) const {
	auto cloned = std::make_shared<Core::Model>(model.GetModelName() + cloned_name_suffix);
	cloned->SetModelType(model.GetModelType());

	for (const auto& layer : model.GetLayers()) {
		auto cloned_layer = CloneLayer(layer);
		if (cloned_layer != nullptr) {
			cloned->AddLayer(cloned_layer);
		}
	}

	if (!model.GetInputShape().empty()) {
		if (cloned->Build(model.GetInputShape()) && model.IsCompiled()) {
			cloned->Compile();
		}
	}

	return cloned;
}

}  // namespace XER::ModelsBuilder::Operations

