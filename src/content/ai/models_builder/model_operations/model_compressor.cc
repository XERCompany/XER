#include "model_compressor.h"

#include "../model_serialization/serializer.h"
#include "../utility/ai_runtime_features.h"

#include <fstream>
#include <iterator>

namespace XER::ModelsBuilder::Operations {

std::string ModelCompressor::CompressModelToString(const Core::Model& model) const {
	const std::string json = Serialization::ModelSerializer::GetInstance().SaveModelToString(model);
	if (json.empty()) {
		return {};
	}
	return Utility::CompressStringFast(json);
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

std::shared_ptr<Core::Model> ModelCompressor::DecompressModel(const std::string& compressed) const {
	const std::string json = Utility::DecompressStringFast(compressed);
	if (json.empty()) {
		return nullptr;
	}
	return Serialization::ModelSerializer::GetInstance().LoadModelFromString(json);
}

std::shared_ptr<Core::Model> ModelCompressor::DecompressModelFromFile(const std::string& input_path) const {
	std::ifstream in(input_path, std::ios::binary);
	if (!in.is_open()) {
		return nullptr;
	}
	const std::string compressed((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	return DecompressModel(compressed);
}

}  // namespace XER::ModelsBuilder::Operations
