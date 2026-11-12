#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <absl/strings/string_view.h>

namespace XER::Flux::Core { class Logger; }
namespace XER::AsyncIO::IO::Async { class IOCacheManager; }

namespace XER::ML::Preprocessing {

/// Abstract base for all preprocessors (operates on float vectors)
class PreprocessBase {
public:
    virtual ~PreprocessBase() = default;

    /// Fit to data (compute statistics / vocabulary etc.)
    virtual void Fit(const std::vector<std::vector<float>>& X) = 0;

    /// Transform data using fitted parameters
    virtual std::vector<std::vector<float>> Transform(
        const std::vector<std::vector<float>>& X) const = 0;

    /// Fit + Transform in one call
    virtual std::vector<std::vector<float>> FitTransform(
        const std::vector<std::vector<float>>& X) {
        Fit(X);
        return Transform(X);
    }

    virtual bool IsFitted() const = 0;
    virtual std::string Name()    const = 0;

    void SetLogger(XER::Flux::Core::Logger* logger) { logger_ = logger; }
    void EnableCache(XER::AsyncIO::IO::Async::IOCacheManager* cache) { cache_ = cache; }
    void SetCacheTtlSeconds(std::uint64_t ttl_seconds) { cache_ttl_seconds_ = ttl_seconds; }

protected:
    void LogInfo(absl::string_view msg) const;
    void LogError(absl::string_view msg) const;

    XER::Flux::Core::Logger* logger_ = nullptr;
    XER::AsyncIO::IO::Async::IOCacheManager* cache_ = nullptr;
    std::uint64_t cache_ttl_seconds_ = 600;
};

}  // namespace XER::ML::Preprocessing
