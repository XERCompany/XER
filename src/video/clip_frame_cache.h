#pragma once

#include "frame.h"

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>

#include "cache/persistent_storage.h"

namespace video {

class ClipFrameCache {
public:
    ClipFrameCache(std::filesystem::path root, std::string key, std::uint32_t ttl_seconds = 21600);

    const std::filesystem::path& root() const { return storage_.root(); }

    bool WriteFrame(std::size_t frame_index, const Frame& frame, std::string* error_out);
    bool ReadFrame(std::size_t frame_index, std::shared_ptr<Frame>* frame_out, std::string* error_out) const;
    bool RemoveFrame(std::size_t frame_index, std::string* error_out);

private:
    Engine::Cache::CacheEntry EntryFor(std::size_t frame_index) const;

    Engine::Cache::PersistentStorage storage_;
    std::string key_;
    std::uint32_t ttl_seconds_ = 0;
};

}  // namespace video
