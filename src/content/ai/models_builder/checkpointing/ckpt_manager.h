#pragma once

#include "ckpt_metadata.h"

#include "../model_core/model.h"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace XER::ModelsBuilder::Checkpointing {

class CheckpointManager {
 public:
	void SetMaxCheckpoints(size_t max_checkpoints);
	size_t GetMaxCheckpoints() const { return max_checkpoints_; }

	bool SaveCheckpoint(const Core::Model& model,
											const std::string& directory,
											CheckpointMetadata metadata) const;

	std::shared_ptr<Core::Model> LoadLatestCheckpoint(const std::string& directory, CheckpointMetadata* out_metadata) const;

	std::shared_ptr<Core::Model> LoadCheckpoint(const std::string& checkpoint_path, CheckpointMetadata* out_metadata) const;

	std::vector<std::string> ListCheckpoints(const std::string& directory) const;
	void PruneOldCheckpoints(const std::string& directory) const;

 private:
	size_t max_checkpoints_ = 5U;
};

}  // namespace XER::ModelsBuilder::Checkpointing

