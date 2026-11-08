#pragma once

#include "ckpt_metadata.h"

#include "../model_core/model.h"

#include <string>

namespace XER::ModelsBuilder::Checkpointing {

class CheckpointRollback {
 public:
  bool RollbackTo(const std::string& checkpoint_path,
				  Core::Model* model,
				  CheckpointMetadata* out_metadata) const;
};

}  // namespace XER::ModelsBuilder::Checkpointing

