#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace XER::ModelsBuilder::Reader::Utils {

class ReaderCacheManager {
 public:
	static ReaderCacheManager& GetInstance();

	void SetMaxBytes(size_t max_bytes);
	size_t GetMaxBytes() const;
	size_t GetCurrentBytes() const;

	bool Put(std::string key, std::vector<uint8_t> data);
	std::optional<std::vector<uint8_t>> Get(const std::string& key) const;
	bool Erase(const std::string& key);
	void Clear();

 private:
	ReaderCacheManager() = default;

	void EvictIfNeededLocked(size_t incoming_size);

	mutable std::mutex mutex_;
	size_t max_bytes_ = 32U * 1024U * 1024U;
	size_t current_bytes_ = 0U;
	std::vector<std::string> insertion_order_;
	std::unordered_map<std::string, std::vector<uint8_t>> cache_;
};

}  // namespace XER::ModelsBuilder::Reader::Utils

