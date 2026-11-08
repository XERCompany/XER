#pragma once

#include "ckpt_metadata.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace XER::ModelsBuilder::Checkpointing {

struct CheckpointSnapshot {
	CheckpointMetadata metadata;
	std::vector<uint8_t> payload;

	bool IsValid() const {
		return !metadata.model_name.empty() && !payload.empty();
	}

	size_t PayloadSize() const { return payload.size(); }
};

}  // namespace XER::ModelsBuilder::Checkpointing

