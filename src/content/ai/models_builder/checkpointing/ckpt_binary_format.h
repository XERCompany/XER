#pragma once

#include "ckpt_snapshot.h"

#include <string>

namespace XER::ModelsBuilder::Checkpointing {

class CheckpointBinaryFormat {
 public:
	static bool WriteToFile(const std::string& path, const CheckpointSnapshot& snapshot);
	static bool ReadFromFile(const std::string& path, CheckpointSnapshot* out_snapshot);

	static constexpr uint32_t kMagic = 0x58434B50U;  // "XCKP"
	static constexpr uint32_t kVersion = 1U;
};

}  // namespace XER::ModelsBuilder::Checkpointing

