#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

namespace XER::ModelsBuilder::Checkpointing {

struct CheckpointMetadata {
	std::string model_name;
	uint64_t epoch = 0U;
	float loss = 0.0f;
	std::string backend;
	uint64_t timestamp_ms = 0U;
	std::unordered_map<std::string, std::string> tags;

	std::string ToText() const;
	static CheckpointMetadata FromText(const std::string& text);
};

uint64_t CurrentTimestampMs();

}  // namespace XER::ModelsBuilder::Checkpointing

