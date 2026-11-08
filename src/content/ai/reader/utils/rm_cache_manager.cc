#include "rm_cache_manager.h"

#include <algorithm>

namespace XER::ModelsBuilder::Reader::Utils {

ReaderCacheManager& ReaderCacheManager::GetInstance() {
	static ReaderCacheManager instance;
	return instance;
}

void ReaderCacheManager::SetMaxBytes(size_t max_bytes) {
	std::scoped_lock lock(mutex_);
	max_bytes_ = std::max<size_t>(1U, max_bytes);
	EvictIfNeededLocked(0U);
}

size_t ReaderCacheManager::GetMaxBytes() const {
	std::scoped_lock lock(mutex_);
	return max_bytes_;
}

size_t ReaderCacheManager::GetCurrentBytes() const {
	std::scoped_lock lock(mutex_);
	return current_bytes_;
}

bool ReaderCacheManager::Put(std::string key, std::vector<uint8_t> data) {
	std::scoped_lock lock(mutex_);
	const size_t incoming = data.size();

	auto existing = cache_.find(key);
	if (existing != cache_.end()) {
		current_bytes_ -= existing->second.size();
		existing->second = std::move(data);
		current_bytes_ += existing->second.size();
		EvictIfNeededLocked(0U);
		return true;
	}

	EvictIfNeededLocked(incoming);
	if (incoming > max_bytes_) {
		return false;
	}

	insertion_order_.push_back(key);
	current_bytes_ += incoming;
	cache_.emplace(std::move(key), std::move(data));
	return true;
}

std::optional<std::vector<uint8_t>> ReaderCacheManager::Get(const std::string& key) const {
	std::scoped_lock lock(mutex_);
	auto it = cache_.find(key);
	if (it == cache_.end()) {
		return std::nullopt;
	}
	return it->second;
}

bool ReaderCacheManager::Erase(const std::string& key) {
	std::scoped_lock lock(mutex_);
	auto it = cache_.find(key);
	if (it == cache_.end()) {
		return false;
	}

	current_bytes_ -= it->second.size();
	cache_.erase(it);
	insertion_order_.erase(
			std::remove(insertion_order_.begin(), insertion_order_.end(), key),
			insertion_order_.end());
	return true;
}

void ReaderCacheManager::Clear() {
	std::scoped_lock lock(mutex_);
	cache_.clear();
	insertion_order_.clear();
	current_bytes_ = 0U;
}

void ReaderCacheManager::EvictIfNeededLocked(size_t incoming_size) {
	while (current_bytes_ + incoming_size > max_bytes_ && !insertion_order_.empty()) {
		const std::string key = insertion_order_.front();
		insertion_order_.erase(insertion_order_.begin());

		auto it = cache_.find(key);
		if (it == cache_.end()) {
			continue;
		}

		current_bytes_ -= it->second.size();
		cache_.erase(it);
	}
}

}  // namespace XER::ModelsBuilder::Reader::Utils

