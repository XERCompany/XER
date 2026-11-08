#include "reader_registry.h"

namespace XER::ModelsBuilder::Reader {

ReaderRegistry& ReaderRegistry::GetInstance() {
	static ReaderRegistry instance;
	return instance;
}

void ReaderRegistry::Register(const std::string& extension, ReaderFactory factory) {
	factories_[extension] = std::move(factory);
}

std::unique_ptr<ReaderBase> ReaderRegistry::Create(const std::string& extension) const {
	const auto it = factories_.find(extension);
	if (it == factories_.end()) {
		return nullptr;
	}
	return it->second();
}

bool ReaderRegistry::Has(const std::string& extension) const {
	return factories_.find(extension) != factories_.end();
}

}  // namespace XER::ModelsBuilder::Reader

