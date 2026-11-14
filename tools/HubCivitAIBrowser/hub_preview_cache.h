#pragma once

#include "ui/layout/geometry.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace XER::Tools::HubBrowser {

struct CacheSummary {
    std::string root_dir;
    std::size_t preview_count = 0;
    uint64_t preview_bytes = 0;
    std::size_t model_count = 0;
    uint64_t model_bytes = 0;
};

struct CachedDiskFile {
    std::string file_name;
    std::string full_path;
    std::string relative_key;  // e.g. "civitai/12345/67890"
    uint64_t size_bytes = 0;
    bool is_archive = false;
};

// Asynchronous Civitai artwork preview cache backed by:
//  - XER::Cache::CacheManager (persistent scope "civitai_previews")
//  - XER::AsyncIO::IO::Async::IOThreadPool (bounded worker queue)
//  - content::civitai::ModelFileDownloader (XER_Network HTTPS streaming)
//  - XER::Bridge::FFmpeg::ImageToJpeg (WebP/AVIF/PNG -> normalized JPEG)
class HubPreviewCache {
public:
    static HubPreviewCache& Instance();

    // Returns local JPEG path when already cached; otherwise schedules an
    // async fetch+transcode when `allow_fetch` is true and returns "".
    std::string GetOrRequest(const std::string& url,
                             int max_dim = 512,
                             bool allow_fetch = true);

    // Returns true if one or more background preview fetches completed since
    // the previous call (used by UI Tick() to trigger repaint).
    bool ConsumeCompleted();

    // Computes aspect-preserving letterbox rect inside `outer`.
    static XER::UI::Layout::Rect ContainRect(const XER::UI::Layout::Rect& outer,
                                             int img_w, int img_h);

    // True when `file_name` has a common archive extension (.zip/.7z/.tar/...).
    static bool HasArchiveExtension(const std::string& file_name);

    // Scans XER::Cache::CacheManager scopes ("civitai_previews" & "models").
    CacheSummary ComputeSummary() const;
    std::vector<CachedDiskFile> ScanCachedModels() const;

    // Deletes cached preview JPEGs and clears in-memory state. Returns count.
    std::size_t ClearPreviewCache();

private:
    HubPreviewCache() = default;

    enum class EntryState { Pending, Ready, Failed };
    struct Entry {
        EntryState state = EntryState::Pending;
        std::string local_path;
    };

    static std::string MakeCacheId(const std::string& url, int max_dim);

    mutable std::mutex mu_;
    std::unordered_map<std::string, Entry> entries_;
    std::atomic<bool> completed_{false};
};

}  // namespace XER::Tools::HubBrowser
