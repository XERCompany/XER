#pragma once
#include <cstdint>
#include <vector>
#include <string>

#include <absl/strings/string_view.h>
#include <range/v3/view.hpp>

// Forward declarations to avoid heavy transitive includes
namespace flux::core { class Logger; }
namespace IO::AsyncIO { class IOCacheManager; }

namespace XER::ML::Loaders {

/// Result of a dataset load: rows × features
struct Dataset {
    std::vector<std::vector<float>> X;  ///< Feature matrix
    std::vector<int>                y;  ///< Labels (may be empty)
};

/// Abstract base for all data loaders
class LoaderBase {
public:
    virtual ~LoaderBase() = default;

    /// Load dataset from file/path
    virtual Dataset Load(const std::string& path) = 0;

    /// Human-readable name
    virtual std::string Name() const = 0;

    /// Inject a flux logger for diagnostic output
    void SetLogger(flux::core::Logger* logger) { logger_ = logger; }

    /// Inject an IO cache manager to cache loaded datasets
    void EnableCache(IO::AsyncIO::IOCacheManager* cache) { cache_ = cache; }

    /// Configure persistent cache time-to-live in seconds
    void SetCacheTtlSeconds(std::uint64_t ttl_seconds) {
        cache_ttl_seconds_ = ttl_seconds;
    }

    /// Disable cache lookups/writes for this loader
    void SetCacheEnabled(bool enabled) { cache_enabled_ = enabled; }

protected:
    void LogInfo(absl::string_view msg) const;
    void LogError(absl::string_view msg) const;

    bool TryLoadCachedDataset(absl::string_view loader_name,
                              absl::string_view source_path,
                              Dataset* out) const;
    void StoreCachedDataset(absl::string_view loader_name,
                            absl::string_view source_path,
                            const Dataset& ds) const;
    void InvalidateCachedDataset(absl::string_view loader_name,
                                 absl::string_view source_path) const;

    flux::core::Logger*          logger_ = nullptr;
    IO::AsyncIO::IOCacheManager* cache_  = nullptr;
    std::uint64_t                cache_ttl_seconds_ = 600;
    bool                         cache_enabled_ = true;
};

}  // namespace XER::ML::Loaders
