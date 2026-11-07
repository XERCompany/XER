#ifndef IO_ASYNC_IO_FILE_MAP_H
#define IO_ASYNC_IO_FILE_MAP_H

#include <cstddef>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace XER::AsyncIO::IO::Async {

class IOFileMap {
public:
    struct MappedRegion {
        size_t offset;
        size_t length;
        std::vector<std::uint8_t> data;
    };

    IOFileMap() = default;

    bool MapRegion(const std::string &path, size_t offset, size_t length);
    bool GetRegion(const std::string &path, size_t offset, size_t length, std::vector<std::uint8_t> &outData);
    void Unmap(const std::string &path);
    size_t GetMappedRegionCount(const std::string &path) const;
    size_t GetTotalMappedRegionCount() const;

private:
    mutable std::mutex mutex_;
    std::map<std::string, std::vector<MappedRegion>> map_; 
};

} // namespace XER::AsyncIO::IO::Async

#endif // IO_ASYNC_IO_FILE_MAP_H
