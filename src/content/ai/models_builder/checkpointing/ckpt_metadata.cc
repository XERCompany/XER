#include "ckpt_metadata.h"

#include <chrono>
#include <sstream>

namespace XER::ModelsBuilder::Checkpointing {

namespace {

std::pair<std::string, std::string> SplitKeyValue(const std::string& line) {
	const size_t pos = line.find('=');
	if (pos == std::string::npos) {
		return {line, std::string()};
	}
	return {line.substr(0, pos), line.substr(pos + 1U)};
}

}  // namespace

std::string CheckpointMetadata::ToText() const {
	std::ostringstream out;
	out << "model_name=" << model_name << "\n";
	out << "epoch=" << epoch << "\n";
	out << "loss=" << loss << "\n";
	out << "backend=" << backend << "\n";
	out << "timestamp_ms=" << timestamp_ms << "\n";
	for (const auto& [key, value] : tags) {
		out << "tag." << key << "=" << value << "\n";
	}
	return out.str();
}

CheckpointMetadata CheckpointMetadata::FromText(const std::string& text) {
	CheckpointMetadata metadata;
	std::istringstream stream(text);
	std::string line;
	while (std::getline(stream, line)) {
		if (line.empty()) {
			continue;
		}

		auto [key, value] = SplitKeyValue(line);
		if (key == "model_name") {
			metadata.model_name = value;
		} else if (key == "epoch") {
			metadata.epoch = static_cast<uint64_t>(std::stoull(value));
		} else if (key == "loss") {
			metadata.loss = std::stof(value);
		} else if (key == "backend") {
			metadata.backend = value;
		} else if (key == "timestamp_ms") {
			metadata.timestamp_ms = static_cast<uint64_t>(std::stoull(value));
		} else if (key.rfind("tag.", 0U) == 0U) {
			metadata.tags[key.substr(4U)] = value;
		}
	}
	return metadata;
}

uint64_t CurrentTimestampMs() {
	const auto now = std::chrono::system_clock::now();
	const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch());
	return static_cast<uint64_t>(ms.count());
}

}  // namespace XER::ModelsBuilder::Checkpointing

