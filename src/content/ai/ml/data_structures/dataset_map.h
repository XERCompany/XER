#pragma once

#include "dataset_base.h"

#include <memory>
#include <string>
#include <unordered_map>

namespace XER::ML::DataStructures {

class DatasetMap {
 public:
	void Put(const std::string& key, std::shared_ptr<DatasetBase> dataset);
	std::shared_ptr<DatasetBase> Get(const std::string& key) const;
	bool Erase(const std::string& key);
	void Clear();

	size_t Size() const { return datasets_.size(); }

 private:
	std::unordered_map<std::string, std::shared_ptr<DatasetBase>> datasets_;
};

}  // namespace XER::ML::DataStructures

