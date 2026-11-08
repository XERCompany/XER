#include "cache.h"

namespace XER::MLData::Caching {

DataCache& DataCache::GetInstance() {
    static DataCache instance;
    return instance;
}

void DataCache::CacheDataset(const std::string& key, std::shared_ptr<Types::Dataset> dataset) {
    if (dataset) {
        cache_[key] = dataset;
    }
}

std::shared_ptr<Types::Dataset> DataCache::GetCachedDataset(const std::string& key) const {
    auto it = cache_.find(key);
    return it != cache_.end() ? it->second : nullptr;
}

bool DataCache::IsCached(const std::string& key) const {
    return cache_.find(key) != cache_.end();
}

void DataCache::ClearCache() {
    cache_.clear();
}

void DataCache::RemoveEntry(const std::string& key) {
    cache_.erase(key);
}

} // namespace XER::MLData::Caching
