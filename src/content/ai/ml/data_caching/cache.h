#pragma once

#include "../data_types/dataset.h"
#include <unordered_map>
#include <memory>

namespace XER::MLData::Caching {

class DataCache {
public:
    static DataCache& GetInstance();
    
    void CacheDataset(const std::string& key, std::shared_ptr<Types::Dataset> dataset);
    std::shared_ptr<Types::Dataset> GetCachedDataset(const std::string& key) const;
    
    bool IsCached(const std::string& key) const;
    void ClearCache();
    void RemoveEntry(const std::string& key);
    
    size_t GetCacheSize() const { return cache_.size(); }

private:
    DataCache() = default;
    std::unordered_map<std::string, std::shared_ptr<Types::Dataset>> cache_;
};

} // namespace XER::MLData::Caching
