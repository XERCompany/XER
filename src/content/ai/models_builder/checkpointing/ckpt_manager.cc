#include "ckpt_manager.h"

#include "ckpt_binary_format.h"

#include "../model_serialization/serializer.h"
#include "../utility/mb_logger.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace XER::ModelsBuilder::Checkpointing {

namespace {

std::string BuildCheckpointFilename(const CheckpointMetadata& metadata) {
	return metadata.model_name + "_e" + std::to_string(metadata.epoch) + "_" +
				 std::to_string(metadata.timestamp_ms) + ".xckpt";
}

std::vector<uint8_t> ReadWholeFileBytes(const std::string& path) {
	std::ifstream in(path, std::ios::binary);
	if (!in.is_open()) {
		return {};
	}
	return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

bool WriteWholeFileBytes(const std::string& path, const std::vector<uint8_t>& bytes) {
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	if (!out.is_open()) {
		return false;
	}
	out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
	return out.good();
}

}  // namespace

void CheckpointManager::SetMaxCheckpoints(size_t max_checkpoints) {
	max_checkpoints_ = std::max<size_t>(1U, max_checkpoints);
}

bool CheckpointManager::SaveCheckpoint(const Core::Model& model,
																			 const std::string& directory,
																			 CheckpointMetadata metadata) const {
	namespace fs = std::filesystem;
	metadata.timestamp_ms = metadata.timestamp_ms == 0U ? CurrentTimestampMs() : metadata.timestamp_ms;
	if (metadata.model_name.empty()) {
		metadata.model_name = model.GetModelName();
	}

	const fs::path dir_path(directory);
	std::error_code ec;
	fs::create_directories(dir_path, ec);
	if (ec) {
		Utility::ModelBuilderLogger::GetInstance().Error(
				"CheckpointManager: cannot create directory: " + directory);
		return false;
	}

	const fs::path temp_model_path = dir_path / "_tmp_model_state.json";
	if (!Serialization::ModelSerializer::GetInstance().SaveModel(temp_model_path.string(), model)) {
		return false;
	}

	const std::vector<uint8_t> payload = ReadWholeFileBytes(temp_model_path.string());
	fs::remove(temp_model_path, ec);
	if (payload.empty()) {
		Utility::ModelBuilderLogger::GetInstance().Error(
				"CheckpointManager: serialized model payload is empty.");
		return false;
	}

	CheckpointSnapshot snapshot;
	snapshot.metadata = std::move(metadata);
	snapshot.payload = payload;

	const fs::path checkpoint_path = dir_path / BuildCheckpointFilename(snapshot.metadata);
	if (!CheckpointBinaryFormat::WriteToFile(checkpoint_path.string(), snapshot)) {
		Utility::ModelBuilderLogger::GetInstance().Error(
				"CheckpointManager: failed to write checkpoint " + checkpoint_path.string());
		return false;
	}

	PruneOldCheckpoints(directory);
	return true;
}

std::shared_ptr<Core::Model> CheckpointManager::LoadLatestCheckpoint(
	const std::string& directory,
	CheckpointMetadata* out_metadata) const {
	const std::vector<std::string> checkpoints = ListCheckpoints(directory);
	if (checkpoints.empty()) {
		return nullptr;
	}
	return LoadCheckpoint(checkpoints.back(), out_metadata);
}

std::shared_ptr<Core::Model> CheckpointManager::LoadCheckpoint(
		const std::string& checkpoint_path,
		CheckpointMetadata* out_metadata) const {
	CheckpointSnapshot snapshot;
	if (!CheckpointBinaryFormat::ReadFromFile(checkpoint_path, &snapshot)) {
		return nullptr;
	}

	namespace fs = std::filesystem;
	const fs::path temp_model_path = fs::path(checkpoint_path).parent_path() / "_tmp_restore_model_state.json";
	if (!WriteWholeFileBytes(temp_model_path.string(), snapshot.payload)) {
		return nullptr;
	}

	std::shared_ptr<Core::Model> model =
			Serialization::ModelSerializer::GetInstance().LoadModel(temp_model_path.string());
	std::error_code ec;
	fs::remove(temp_model_path, ec);

	if (out_metadata != nullptr) {
		*out_metadata = snapshot.metadata;
	}
	return model;
}

std::vector<std::string> CheckpointManager::ListCheckpoints(const std::string& directory) const {
	namespace fs = std::filesystem;
	std::vector<std::string> files;

	const fs::path dir_path(directory);
	std::error_code ec;
	if (!fs::exists(dir_path, ec)) {
		return files;
	}

	for (const auto& entry : fs::directory_iterator(dir_path, ec)) {
		if (ec) {
			break;
		}
		if (entry.is_regular_file() && entry.path().extension() == ".xckpt") {
			files.push_back(entry.path().string());
		}
	}

	std::sort(files.begin(), files.end());
	return files;
}

void CheckpointManager::PruneOldCheckpoints(const std::string& directory) const {
	namespace fs = std::filesystem;
	std::vector<std::string> checkpoints = ListCheckpoints(directory);
	if (checkpoints.size() <= max_checkpoints_) {
		return;
	}

	const size_t to_remove = checkpoints.size() - max_checkpoints_;
	for (size_t i = 0; i < to_remove; ++i) {
		std::error_code ec;
		fs::remove(fs::path(checkpoints[i]), ec);
	}
}

}  // namespace XER::ModelsBuilder::Checkpointing

