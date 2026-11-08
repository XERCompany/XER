#include "ckpt_binary_format.h"

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace XER::ModelsBuilder::Checkpointing {

namespace {

template <typename T>
bool WritePod(std::ofstream& out, const T& value) {
	out.write(reinterpret_cast<const char*>(&value), sizeof(T));
	return out.good();
}

template <typename T>
bool ReadPod(std::ifstream& in, T* value) {
	in.read(reinterpret_cast<char*>(value), sizeof(T));
	return in.good();
}

}  // namespace

bool CheckpointBinaryFormat::WriteToFile(const std::string& path, const CheckpointSnapshot& snapshot) {
	if (!snapshot.IsValid()) {
		return false;
	}

	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	if (!out.is_open()) {
		return false;
	}

	const std::string metadata_blob = snapshot.metadata.ToText();
	const uint64_t metadata_size = static_cast<uint64_t>(metadata_blob.size());
	const uint64_t payload_size = static_cast<uint64_t>(snapshot.payload.size());

	if (!WritePod(out, kMagic) ||
			!WritePod(out, kVersion) ||
			!WritePod(out, metadata_size) ||
			!WritePod(out, payload_size)) {
		return false;
	}

	out.write(metadata_blob.data(), static_cast<std::streamsize>(metadata_blob.size()));
	out.write(reinterpret_cast<const char*>(snapshot.payload.data()),
						static_cast<std::streamsize>(snapshot.payload.size()));
	return out.good();
}

bool CheckpointBinaryFormat::ReadFromFile(const std::string& path, CheckpointSnapshot* out_snapshot) {
	if (out_snapshot == nullptr) {
		return false;
	}

	std::ifstream in(path, std::ios::binary);
	if (!in.is_open()) {
		return false;
	}

	uint32_t magic = 0U;
	uint32_t version = 0U;
	uint64_t metadata_size = 0U;
	uint64_t payload_size = 0U;

	if (!ReadPod(in, &magic) || !ReadPod(in, &version) ||
			!ReadPod(in, &metadata_size) || !ReadPod(in, &payload_size)) {
		return false;
	}

	if (magic != kMagic || version != kVersion) {
		return false;
	}

	std::string metadata_blob(metadata_size, '\0');
	std::vector<uint8_t> payload(payload_size, 0U);
	in.read(metadata_blob.data(), static_cast<std::streamsize>(metadata_size));
	in.read(reinterpret_cast<char*>(payload.data()), static_cast<std::streamsize>(payload_size));
	if (!in.good()) {
		return false;
	}

	out_snapshot->metadata = CheckpointMetadata::FromText(metadata_blob);
	out_snapshot->payload = std::move(payload);
	return out_snapshot->IsValid();
}

}  // namespace XER::ModelsBuilder::Checkpointing

