#include "cache_manager.h"

#include <algorithm>

namespace XER::ML::DataStructures {

void CacheManager::SetMaxEntries(size_t max_entries) {
	max_entries_ = std::max<size_t>(1U, max_entries);
	EvictIfNeeded();
}

void CacheManager::Put(const std::string& key, std::vector<float> value) {
	if (key.empty()) {
		return;
	}

	auto it = cache_.find(key);
	if (it == cache_.end()) {
		order_.push_back(key);
		cache_.emplace(key, std::move(value));
	} else {
		it->second = std::move(value);
	}
	EvictIfNeeded();
}

std::optional<std::vector<float>> CacheManager::Get(const std::string& key) const {
	auto it = cache_.find(key);
	if (it == cache_.end()) {
		return std::nullopt;
	}
	return it->second;
}

bool CacheManager::Erase(const std::string& key) {
	const size_t erased = cache_.erase(key);
	if (erased > 0U) {
		order_.erase(std::remove(order_.begin(), order_.end(), key), order_.end());
		return true;
	}
	return false;
}

void CacheManager::Clear() {
	cache_.clear();
	order_.clear();
}

void CacheManager::EvictIfNeeded() {
	while (cache_.size() > max_entries_ && !order_.empty()) {
		const std::string key = order_.front();
		order_.erase(order_.begin());
		cache_.erase(key);
	}
}

}  // namespace XER::ML::DataStructures

