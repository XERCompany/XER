#include "hub_preview_cache.h"

#include "async_io/io_thread_pool.h"
#include "cache/cache_manager.h"
#include "content/civitaimodels/civitai_model_downloader.h"
#include "wrapper/ffmpeg/ffmpeg_engine_bridge.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <system_error>

namespace XER::Tools::HubBrowser {

namespace {

constexpr const char* kPreviewScope = "civitai_previews";
constexpr const char* kPreviewKey = "img";

uint64_t Fnv1a64(const std::string& s) {
    uint64_t h = 14695981039346656037ULL;
    for (unsigned char c : s) {
        h ^= static_cast<uint64_t>(c);
        h *= 1099511628211ULL;
    }
    return h;
}

std::string ToLowerAscii(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
    }
    return s;
}

}  // namespace

HubPreviewCache& HubPreviewCache::Instance() {
    static HubPreviewCache inst;
    return inst;
}

std::string HubPreviewCache::MakeCacheId(const std::string& url, int max_dim) {
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%016llx_%d",
                  static_cast<unsigned long long>(Fnv1a64(url)),
                  max_dim);
    return buf;
}

std::string HubPreviewCache::GetOrRequest(const std::string& url,
                                          int max_dim,
                                          bool allow_fetch) {
    if (url.empty()) return {};

    const std::string id = MakeCacheId(url, max_dim);
    const std::string jpg_name = id + ".jpg";

    {
        std::lock_guard<std::mutex> lock(mu_);
        auto it = entries_.find(id);
        if (it != entries_.end()) {
            return it->second.state == EntryState::Ready
                       ? it->second.local_path
                       : std::string{};
        }
    }

    auto& cm = XER::Cache::CacheManager::Instance();
    const std::filesystem::path jpg_path =
        cm.PathFor(kPreviewScope, kPreviewKey, jpg_name);
    std::error_code ec;
    if (std::filesystem::exists(jpg_path, ec) &&
        std::filesystem::file_size(jpg_path, ec) > 32) {
        const std::string path_str = jpg_path.string();
        std::lock_guard<std::mutex> lock(mu_);
        entries_[id] = Entry{EntryState::Ready, path_str};
        return path_str;
    }

    if (!allow_fetch) return {};

    {
        std::lock_guard<std::mutex> lock(mu_);
        if (entries_.find(id) != entries_.end()) return {};
        entries_[id] = Entry{EntryState::Pending, {}};
    }

    auto& pool = XER::AsyncIO::IO::Async::IOThreadPool::GetSharedInstance();
    const bool queued = pool.TryEnqueue(
        [this, url, id, jpg_name, max_dim]() {
            auto& cache = XER::Cache::CacheManager::Instance();
            const std::string raw_name = id + ".raw";
            const std::filesystem::path final_jpg =
                cache.PathFor(kPreviewScope, kPreviewKey, jpg_name);

            content::civitai::ModelFileDownloader::Options opts;
            opts.scope = kPreviewScope;
            opts.key = kPreviewKey;
            opts.file_name = raw_name;
            opts.verify_checksum = false;
            opts.max_retries = 2;
            opts.headers["User-Agent"] = "XER-ModelHub/1.0";

            std::string dl_err;
            std::filesystem::path raw_path;
            const bool downloaded =
                content::civitai::ModelFileDownloader().DownloadSync(
                    url, opts, {}, &dl_err, &raw_path);

            bool ok = false;
            if (downloaded && !raw_path.empty()) {
                std::error_code fs_ec;
                std::filesystem::create_directories(final_jpg.parent_path(),
                                                    fs_ec);
                std::string conv_err;
                if (XER::Bridge::FFmpeg::IsAvailable() &&
                    XER::Bridge::FFmpeg::ImageToJpeg(
                        raw_path.string(), final_jpg.string(), max_dim,
                        &conv_err)) {
                    ok = std::filesystem::exists(final_jpg, fs_ec) &&
                         std::filesystem::file_size(final_jpg, fs_ec) > 32;
                }
                if (!ok) {
                    // Fallback: keep raw payload directly (WIC decodes
                    // JPEG/PNG/WebP natively on Windows 10/11).
                    std::filesystem::copy_file(
                        raw_path, final_jpg,
                        std::filesystem::copy_options::overwrite_existing,
                        fs_ec);
                    ok = !fs_ec && std::filesystem::exists(final_jpg, fs_ec) &&
                         std::filesystem::file_size(final_jpg, fs_ec) > 32;
                }
                std::filesystem::remove(raw_path, fs_ec);
            }

            {
                std::lock_guard<std::mutex> lock(mu_);
                entries_[id] = Entry{
                    ok ? EntryState::Ready : EntryState::Failed,
                    ok ? final_jpg.string() : std::string{}};
            }
            completed_.store(true);
        },
        48);

    if (!queued) {
        // Queue full while scrolling rapidly — drop Pending so it retries later.
        std::lock_guard<std::mutex> lock(mu_);
        entries_.erase(id);
    }
    return {};
}

bool HubPreviewCache::ConsumeCompleted() {
    return completed_.exchange(false);
}

XER::UI::Layout::Rect HubPreviewCache::ContainRect(
    const XER::UI::Layout::Rect& outer, int img_w, int img_h) {
    if (outer.width <= 0.0f || outer.height <= 0.0f ||
        img_w <= 0 || img_h <= 0) {
        return outer;
    }
    const float aspect = static_cast<float>(img_w) / static_cast<float>(img_h);
    const float box_aspect = outer.width / outer.height;
    float w = outer.width;
    float h = outer.height;
    if (aspect > box_aspect) {
        h = w / aspect;
    } else {
        w = h * aspect;
    }
    return XER::UI::Layout::Rect{
        outer.x + (outer.width - w) * 0.5f,
        outer.y + (outer.height - h) * 0.5f,
        w,
        h};
}

bool HubPreviewCache::HasArchiveExtension(const std::string& file_name) {
    const std::string low = ToLowerAscii(file_name);
    static const char* kExts[] = {
        ".zip", ".7z", ".rar", ".tar", ".tgz",
        ".tar.gz", ".tar.bz2", ".tar.xz", ".tar.zst", ".cab"};
    for (const char* ext : kExts) {
        const std::size_t n = std::strlen(ext);
        if (low.size() >= n &&
            low.compare(low.size() - n, n, ext) == 0) {
            return true;
        }
    }
    return false;
}

CacheSummary HubPreviewCache::ComputeSummary() const {
    CacheSummary out;
    const auto& root = XER::Cache::CacheManager::Instance().RootDirectory();
    out.root_dir = root.string();

    auto accumulate = [](const std::filesystem::path& dir,
                         std::size_t* count, uint64_t* bytes) {
        std::error_code ec;
        if (!std::filesystem::exists(dir, ec)) return;
        for (std::filesystem::recursive_directory_iterator it(dir, ec), end;
             it != end && !ec; it.increment(ec)) {
            if (it->is_regular_file(ec)) {
                ++(*count);
                *bytes += static_cast<uint64_t>(it->file_size(ec));
            }
        }
    };

    accumulate(root / kPreviewScope, &out.preview_count, &out.preview_bytes);
    accumulate(root / "models", &out.model_count, &out.model_bytes);
    return out;
}

std::vector<CachedDiskFile> HubPreviewCache::ScanCachedModels() const {
    std::vector<CachedDiskFile> out;
    const auto models_dir =
        XER::Cache::CacheManager::Instance().RootDirectory() / "models";
    std::error_code ec;
    if (!std::filesystem::exists(models_dir, ec)) return out;

    for (std::filesystem::recursive_directory_iterator it(models_dir, ec), end;
         it != end && !ec; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        const auto& p = it->path();
        const std::string fname = p.filename().string();
        if (fname == "manifest.json" ||
            (fname.size() > 5 &&
             fname.compare(fname.size() - 5, 5, ".part") == 0)) {
            continue;
        }
        CachedDiskFile entry;
        entry.file_name = fname;
        entry.full_path = p.string();
        entry.size_bytes = static_cast<uint64_t>(it->file_size(ec));
        entry.is_archive = HasArchiveExtension(fname);
        const auto rel = std::filesystem::relative(p.parent_path(), models_dir, ec);
        entry.relative_key = ec ? std::string{} : rel.generic_string();
        out.push_back(std::move(entry));
    }
    std::sort(out.begin(), out.end(),
              [](const CachedDiskFile& a, const CachedDiskFile& b) {
                  return a.file_name < b.file_name;
              });
    return out;
}

std::size_t HubPreviewCache::ClearPreviewCache() {
    std::size_t removed = 0;
    const auto dir =
        XER::Cache::CacheManager::Instance().RootDirectory() / kPreviewScope;
    std::error_code ec;
    if (std::filesystem::exists(dir, ec)) {
        for (std::filesystem::recursive_directory_iterator it(dir, ec), end;
             it != end && !ec; it.increment(ec)) {
            if (it->is_regular_file(ec)) ++removed;
        }
        std::filesystem::remove_all(dir, ec);
    }
    std::lock_guard<std::mutex> lock(mu_);
    entries_.clear();
    return removed;
}

}  // namespace XER::Tools::HubBrowser
