#include "io_file_map.h"
#include <fstream>

namespace AsyncIO::IO::Async {

bool IOFileMap::MapRegion(const std::string &path, size_t offset, size_t length) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return false;
    }

    input.seekg(0, std::ios::end);
    size_t fileSize = static_cast<size_t>(input.tellg());
    if (offset > fileSize) {
        return false;
    }

    size_t readLen = std::min(length, fileSize - offset);
    input.seekg(offset, std::ios::beg);

    std::vector<std::uint8_t> buffer(readLen);
    input.read(reinterpret_cast<char *>(buffer.data()), readLen);
    if (!input) {
        return false;
    }

    std::lock_guard lock(mutex_);
    map_[path].push_back({offset, readLen, std::move(buffer)});
    return true;
}

bool IOFileMap::GetRegion(const std::string &path, size_t offset, size_t length, std::vector<std::uint8_t> &outData) {
    std::lock_guard lock(mutex_);
    auto it = map_.find(path);
    if (it == map_.end()) {
        return false;
    }

    for (auto const &region : it->second) {
        if (region.offset <= offset && offset + length <= region.offset + region.length) {
            size_t relative = offset - region.offset;
            outData.assign(region.data.begin() + relative, region.data.begin() + relative + length);
            return true;
        }
    }
    return false;
}

void IOFileMap::Unmap(const std::string &path) {
    std::lock_guard lock(mutex_);
    map_.erase(path);
}

size_t IOFileMap::GetMappedRegionCount(const std::string &path) const {
    std::lock_guard lock(mutex_);
    auto it = map_.find(path);
    return it == map_.end() ? 0 : it->second.size();
}

size_t IOFileMap::GetTotalMappedRegionCount() const {
    std::lock_guard lock(mutex_);
    size_t total = 0;
    for (const auto& [_, regions] : map_) {
        total += regions.size();
    }
    return total;
}

} // namespace AsyncIO::IO::Async
