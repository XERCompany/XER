#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace XER::Flux::Core { class Logger; }
namespace XER::AsyncIO::IO::Async { class IOCacheManager; }

namespace XER::ML::Validation {

/// Aggregate metric report (accuracy, precision, recall, F1)
struct ValidationMetrics {
    float accuracy       = 0.0f;
    float precision      = 0.0f;
    float recall         = 0.0f;
    float f1_score       = 0.0f;
    float macro_f1       = 0.0f;
    float weighted_f1    = 0.0f;
    size_t n_samples     = 0;

    std::string ToString() const;
};

struct ValMetrics {
    static void SetLogger(XER::Flux::Core::Logger* logger);
    static void EnableCache(XER::AsyncIO::IO::Async::IOCacheManager* cache);
    static void SetCacheTtlSeconds(std::uint64_t ttl_seconds);
    static void InvalidateCache(const std::vector<int>& predictions,
                                const std::vector<int>& ground_truth,
                                int positive_class = 1);

    /// Compute all metrics at once
    static ValidationMetrics Compute(const std::vector<int>& predictions,
                                      const std::vector<int>& ground_truth,
                                      int positive_class = 1);
};

}  // namespace XER::ML::Validation
