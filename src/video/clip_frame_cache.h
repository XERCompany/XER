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
    // `scope` namespaces the on-disk directory (default preserves the
    // original caller's layout). Callers sharing a PersistentStorage
    // root pick their own scope to isolate sweeps/eviction.
    ClipFrameCache(std::filesystem::path root, std::string key,
                   std::uint32_t ttl_seconds = 21600,
                   std::string scope = "video_music_bass_eq_frames");

    const std::filesystem::path& root() const { return storage_.root(); }
    const std::string& scope() const { return scope_; }

    bool WriteFrame(std::size_t frame_index, const Frame& frame, std::string* error_out);
    bool ReadFrame(std::size_t frame_index, std::shared_ptr<Frame>* frame_out, std::string* error_out) const;
    bool RemoveFrame(std::size_t frame_index, std::string* error_out);

    // Existence check honoring TTL (expired entries are evicted and
    // report false). On-disk path for a slot — callers tracking cache
    // size use it to stat the written file.
    bool HasFrame(std::size_t frame_index) const;
    std::filesystem::path PathForFrame(std::size_t frame_index) const;

private:
    XER::Cache::CacheEntry EntryFor(std::size_t frame_index) const;

    XER::Cache::PersistentStorage storage_;
    std::string key_;
    std::string scope_;
    std::uint32_t ttl_seconds_ = 0;
};

}  // namespace video
