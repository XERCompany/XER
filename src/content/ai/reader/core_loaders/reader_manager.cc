#include "reader_manager.h"

#include "reader_factory.h"
#include "reader_registry.h"

#include "../framework_adapters/tf_saved_model_parser.h"

namespace XER::ModelsBuilder::Reader {

ReaderManager::ReaderManager() {
	ReaderFactory::RegisterDefaults();
}

LoadResult ReaderManager::Load(const std::string& filepath) {
	std::unique_ptr<ReaderBase> reader = CreateReader(filepath);
	if (!reader) {
		return LoadResult{nullptr, false, "No reader registered for: " + filepath};
	}
	return reader->Load(filepath);
}

std::unique_ptr<ReaderBase> ReaderManager::CreateReader(const std::string& filepath) const {
	const std::string extension = ExtractExtension(filepath);
	if (!extension.empty()) {
		if (std::unique_ptr<ReaderBase> by_extension = ReaderRegistry::GetInstance().Create(extension)) {
			return by_extension;
		}
	}

	// Extensionless fallback: directory-based formats such as TensorFlow SavedModel.
	auto tf_saved = std::make_unique<Framework::TfSavedModelParser>();
	if (tf_saved->CanLoad(filepath)) {
		return tf_saved;
	}

	return nullptr;
}

std::string ReaderManager::ExtractExtension(const std::string& filepath) {
	const size_t dot = filepath.find_last_of('.');
	if (dot == std::string::npos) {
		return {};
	}
	return filepath.substr(dot);
}

}  // namespace XER::ModelsBuilder::Reader

