#include "io_cache_manager.h"

namespace AsyncIO::IO::Async {

IOCacheManager::IOCacheManager(size_t capacityBytes)
    : capacityBytes_(capacityBytes), sizeBytes_(0) {}

void IOCacheManager::Put(const std::string &key, std::vector<std::uint8_t> data) {
    std::lock_guard lock(mutex_);
    auto it = cacheMap_.find(key);
    if (it != cacheMap_.end()) {
        sizeBytes_ -= it->second->value.size();
        cacheList_.erase(it->second);
        cacheMap_.erase(it);
    }

    sizeBytes_ += data.size();
    cacheList_.push_front({key, std::move(data)});
    cacheMap_[key] = cacheList_.begin();
    EvictIfNeeded();
}

bool IOCacheManager::Get(const std::string &key, std::vector<std::uint8_t> &outData) {
    std::lock_guard lock(mutex_);
    auto it = cacheMap_.find(key);
    if (it == cacheMap_.end()) {
        return false;
    }

    cacheList_.splice(cacheList_.begin(), cacheList_, it->second);
    outData = it->second->value;
    return true;
}

void IOCacheManager::Clear() {
    std::lock_guard lock(mutex_);
    cacheList_.clear();
    cacheMap_.clear();
    sizeBytes_ = 0;
}

void IOCacheManager::RemoveByPrefix(const std::string& prefix) {
    std::lock_guard lock(mutex_);
    for (auto it = cacheList_.begin(); it != cacheList_.end();) {
        if (it->key.rfind(prefix, 0) == 0) {
            sizeBytes_ -= it->value.size();
            cacheMap_.erase(it->key);
            it = cacheList_.erase(it);
            continue;
        }
        ++it;
    }
}

size_t IOCacheManager::GetEntryCount() const {
    std::lock_guard lock(mutex_);
    return cacheMap_.size();
}

size_t IOCacheManager::GetSizeBytes() const {
    std::lock_guard lock(mutex_);
    return sizeBytes_;
}

void IOCacheManager::EvictIfNeeded() {
    while (sizeBytes_ > capacityBytes_ && !cacheList_.empty()) {
        auto &entry = cacheList_.back();
        sizeBytes_ -= entry.value.size();
        cacheMap_.erase(entry.key);
        cacheList_.pop_back();
    }
}

} // namespace AsyncIO::IO::Async
