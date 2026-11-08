#include "model_merger.h"

#include "model_cloner.h"

namespace XER::ModelsBuilder::Operations {

std::shared_ptr<Core::Model> ModelMerger::MergeSequential(const Core::Model& first,
    const Core::Model& second,
    const std::string& merged_name) const {
	ModelCloner cloner;
	auto merged = std::make_shared<Core::Model>(merged_name);
	merged->SetModelType(Core::ModelType::Sequential);

	auto first_clone = cloner.Clone(first, "");
	auto second_clone = cloner.Clone(second, "");
	for (const auto& layer : first_clone->GetLayers()) {
		merged->AddLayer(layer);
	}
	for (const auto& layer : second_clone->GetLayers()) {
		merged->AddLayer(layer);
	}

	if (!first.GetInputShape().empty()) {
		if (merged->Build(first.GetInputShape()) && (first.IsCompiled() || second.IsCompiled())) {
			merged->Compile();
		}
	}

	return merged;
}

}  // namespace XER::ModelsBuilder::Operations

