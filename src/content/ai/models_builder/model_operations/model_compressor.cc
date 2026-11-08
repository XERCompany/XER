#include "model_compressor.h"

#include "../model_serialization/serializer.h"
#include "../utility/ai_runtime_features.h"

#include <filesystem>
#include <fstream>
#include <iterator>

namespace XER::ModelsBuilder::Operations {

std::string ModelCompressor::CompressModelToString(const Core::Model& model) const {
	namespace fs = std::filesystem;
	const fs::path temp_path = fs::temp_directory_path() / "xer_model_compress_tmp.json";

	if (!Serialization::ModelSerializer::GetInstance().SaveModel(temp_path.string(), model)) {
		return std::string();
	}

	std::ifstream in(temp_path, std::ios::binary);
	const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

	std::error_code ec;
	fs::remove(temp_path, ec);
	return Utility::CompressStringFast(text);
}

bool ModelCompressor::CompressModelToFile(const Core::Model& model, const std::string& output_path) const {
	const std::string compressed = CompressModelToString(model);
	if (compressed.empty()) {
		return false;
	}

	std::ofstream out(output_path, std::ios::binary | std::ios::trunc);
	if (!out.is_open()) {
		return false;
	}

	out.write(compressed.data(), static_cast<std::streamsize>(compressed.size()));
	return out.good();
}

}  // namespace XER::ModelsBuilder::Operations

