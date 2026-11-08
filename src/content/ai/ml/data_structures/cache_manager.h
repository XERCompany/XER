#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace XER::ML::DataStructures {

class CacheManager {
 public:
	void SetMaxEntries(size_t max_entries);
	size_t MaxEntries() const { return max_entries_; }
	size_t Size() const { return cache_.size(); }

	void Put(const std::string& key, std::vector<float> value);
	std::optional<std::vector<float>> Get(const std::string& key) const;
	bool Erase(const std::string& key);
	void Clear();

 private:
	void EvictIfNeeded();

	size_t max_entries_ = 256U;
	std::vector<std::string> order_;
	std::unordered_map<std::string, std::vector<float>> cache_;
};

}  // namespace XER::ML::DataStructures

