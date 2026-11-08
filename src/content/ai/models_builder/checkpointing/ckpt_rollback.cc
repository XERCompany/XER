#include "ckpt_rollback.h"

#include "ckpt_manager.h"

namespace XER::ModelsBuilder::Checkpointing {

bool CheckpointRollback::RollbackTo(const std::string& checkpoint_path,
        Core::Model* model,
        CheckpointMetadata* out_metadata) const {
	if (model == nullptr) {
		return false;
	}

	CheckpointManager manager;
	std::shared_ptr<Core::Model> restored_model = manager.LoadCheckpoint(checkpoint_path, out_metadata);
	if (!restored_model) {
		return false;
	}

	*model = *restored_model;
	return true;
}

}  // namespace XER::ModelsBuilder::Checkpointing

