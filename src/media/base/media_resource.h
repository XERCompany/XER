/**
 * MediaResource — pooled shared media buffer. Decoded frames may carry a
 * MediaResource instead of copying pixel/sample data; users keep the frame
 * alive through the shared handle.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace XER::Media {

class MediaResource {
public:
    static std::shared_ptr<MediaResource> Allocate(std::size_t bytes);

    std::uint8_t* data() { return buf_.data(); }
    const std::uint8_t* data() const { return buf_.data(); }
    std::size_t size() const { return buf_.size(); }

    std::uint8_t* WritableData(std::size_t bytes) {
        if (buf_.size() < bytes) buf_.resize(bytes);
        return buf_.data();
    }

private:
    std::vector<std::uint8_t> buf_;
};

// Free-list pool of MediaResource buffers for hot-path reuse
// (allocations are forbidden inside audio callbacks).
class MediaResourcePool {
public:
    explicit MediaResourcePool(std::size_t max_pooled = 16)
        : max_pooled_(max_pooled) {}

    std::shared_ptr<MediaResource> Acquire(std::size_t bytes);
    void Release(std::shared_ptr<MediaResource> res);

private:
    const std::size_t max_pooled_;
    std::mutex mu_;
    std::vector<std::shared_ptr<MediaResource>> pool_;
};

}  // namespace XER::Media
