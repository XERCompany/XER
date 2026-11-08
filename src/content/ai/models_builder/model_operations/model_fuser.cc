#include "model_fuser.h"

#include "model_cloner.h"

namespace XER::ModelsBuilder::Operations {

std::shared_ptr<Core::Model> ModelFuser::Fuse(const Core::Model& model) const {
	// Keep behavior conservative until additional layer-specific fuse rules are added.
	ModelCloner cloner;
	return cloner.Clone(model, "_fused");
}

}  // namespace XER::ModelsBuilder::Operations

