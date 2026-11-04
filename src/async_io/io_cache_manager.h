#ifndef IO_ASYNC_IO_CACHE_MANAGER_H
#define IO_ASYNC_IO_CACHE_MANAGER_H

#include <cstddef>
#include <cstdint>
#include <list>
#include <mutex>
#include <unordered_map>
#include <string>
#include <vector>

namespace AsyncIO::IO::Async {

class IOCacheManager {
public:
    explicit IOCacheManager(size_t capacityBytes = 8 * 1024 * 1024);

    IOCacheManager(const IOCacheManager &) = delete;
    IOCacheManager &operator=(const IOCacheManager &) = delete;

    void Put(const std::string &key, std::vector<std::uint8_t> data);
    bool Get(const std::string &key, std::vector<std::uint8_t> &outData);
    void Clear();
    void RemoveByPrefix(const std::string& prefix);
    size_t GetEntryCount() const;
    size_t GetSizeBytes() const;

private:
    struct Entry {
        std::string key;
        std::vector<std::uint8_t> value;
    };

    size_t capacityBytes_;
    size_t sizeBytes_ = 0;
    std::list<Entry> cacheList_;
    std::unordered_map<std::string, std::list<Entry>::iterator> cacheMap_;
    mutable std::mutex mutex_;

    void EvictIfNeeded();
};

} // namespace AsyncIO::IO::Async

#endif // IO_ASYNC_IO_CACHE_MANAGER_H
