#include "io_prefetch_manager.h"
#include <sstream>

namespace XER::AsyncIO::IO::Async {

std::string IOPrefetchManager::BuildRegionKey(const std::string& path, size_t offset, size_t length) {
    std::ostringstream os;
    os << path << ':' << offset << ':' << length;
    return os.str();
}

IOPrefetchManager::IOPrefetchManager(size_t threadCount, size_t cacheSize)
    : cache_(cacheSize) {
    (void)threadCount;
}

bool IOPrefetchManager::Prefetch(const std::string &path, size_t offset, size_t length) {
    return PrefetchAsync(path, offset, length, nullptr);
}

bool IOPrefetchManager::PrefetchAsync(const std::string &path, size_t offset, size_t length, PrefetchCallback callback) {
    const std::string key = BuildRegionKey(path, offset, length);
    std::uint64_t path_generation = 0;

    {
        std::lock_guard lock(mutex_);
        if (inProgress_.count(key) > 0) {
            return false;
        }
        path_generation = pathGenerations_[path];
        inProgress_.insert(key);
    }

    IOThreadPool::GetSharedInstance().Enqueue([this, path, offset, length, key, path_generation, callback = std::move(callback)]() mutable {
        bool success = false;
        bool stale_result = false;
        std::vector<std::uint8_t> data;
        if (fileMap_.MapRegion(path, offset, length) && fileMap_.GetRegion(path, offset, length, data)) {
            std::lock_guard lock(mutex_);
            stale_result = pathGenerations_[path] != path_generation;
        }
        if (!stale_result && !data.empty()) {
            cache_.Put(key, data);
            success = true;
        } else if (stale_result) {
            fileMap_.Unmap(path);
        }
        {
            std::lock_guard lock(mutex_);
            inProgress_.erase(key);
        }
        if (callback) {
            callback(success);
        }
    });

    return true;
}

bool IOPrefetchManager::TryGet(const std::string &path, size_t offset, size_t length, std::vector<std::uint8_t> &out) {
    const std::string key = BuildRegionKey(path, offset, length);

    if (cache_.Get(key, out)) {
        return true;
    }

    if (fileMap_.GetRegion(path, offset, length, out)) {
        cache_.Put(key, out);
        return true;
    }

    return false;
}

void IOPrefetchManager::InvalidatePath(const std::string& path) {
    {
        std::lock_guard lock(mutex_);
        ++pathGenerations_[path];
    }

    cache_.RemoveByPrefix(path + ':');
    fileMap_.Unmap(path);
}

bool IOPrefetchManager::IsPrefetchInProgress(const std::string& path, size_t offset, size_t length) const {
    std::lock_guard lock(mutex_);
    return inProgress_.count(BuildRegionKey(path, offset, length)) > 0;
}

size_t IOPrefetchManager::GetInProgressCount() const {
    std::lock_guard lock(mutex_);
    return inProgress_.size();
}

size_t IOPrefetchManager::GetCachedEntryCount() const {
    return cache_.GetEntryCount();
}

size_t IOPrefetchManager::GetCacheSizeBytes() const {
    return cache_.GetSizeBytes();
}

size_t IOPrefetchManager::GetMappedRegionCount() const {
    return fileMap_.GetTotalMappedRegionCount();
}

} // namespace XER::AsyncIO::IO::Async
