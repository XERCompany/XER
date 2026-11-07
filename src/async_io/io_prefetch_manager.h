#ifndef IO_ASYNC_IO_PREFETCH_MANAGER_H
#define IO_ASYNC_IO_PREFETCH_MANAGER_H

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "io_cache_manager.h"
#include "io_file_map.h"
#include "io_thread_pool.h"

namespace XER::AsyncIO::IO::Async {

using PrefetchCallback = std::function<void(bool)>;

class IOPrefetchManager {
public:
    IOPrefetchManager(size_t threadCount = 2, size_t cacheSize = 8 * 1024 * 1024);

    bool Prefetch(const std::string &path, size_t offset, size_t length);
    bool PrefetchAsync(const std::string &path, size_t offset, size_t length, PrefetchCallback callback);
    bool TryGet(const std::string &path, size_t offset, size_t length, std::vector<std::uint8_t> &out);
    void InvalidatePath(const std::string& path);
    bool IsPrefetchInProgress(const std::string& path, size_t offset, size_t length) const;
    size_t GetInProgressCount() const;
    size_t GetCachedEntryCount() const;
    size_t GetCacheSizeBytes() const;
    size_t GetMappedRegionCount() const;

private:
    static std::string BuildRegionKey(const std::string& path, size_t offset, size_t length);

    IOCacheManager cache_;
    IOFileMap fileMap_;
    mutable std::mutex mutex_;
    std::unordered_set<std::string> inProgress_;
    std::unordered_map<std::string, std::uint64_t> pathGenerations_;
};

} // namespace XER::AsyncIO::IO::Async

#endif // IO_ASYNC_IO_PREFETCH_MANAGER_H
