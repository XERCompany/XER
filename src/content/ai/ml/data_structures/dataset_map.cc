#include "dataset_map.h"

namespace XER::ML::DataStructures {

void DatasetMap::Put(const std::string& key, std::shared_ptr<DatasetBase> dataset) {
	if (!key.empty() && dataset != nullptr) {
		datasets_[key] = std::move(dataset);
	}
}

std::shared_ptr<DatasetBase> DatasetMap::Get(const std::string& key) const {
	const auto it = datasets_.find(key);
	if (it == datasets_.end()) {
		return nullptr;
	}
	return it->second;
}

bool DatasetMap::Erase(const std::string& key) {
	return datasets_.erase(key) > 0U;
}

void DatasetMap::Clear() {
	datasets_.clear();
}

}  // namespace XER::ML::DataStructures

