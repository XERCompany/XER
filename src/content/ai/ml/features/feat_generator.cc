#include "feat_generator.h"

// Optional cache integration via IO::AsyncIO::IOCacheManager
#if __has_include("async_io/io_cache_manager.h")
#  include "async_io/io_cache_manager.h"
#  define HAS_IO_CACHE 1
#else
#  define HAS_IO_CACHE 0
#endif

#include <cstring>

namespace XER::ML::Features {

#if HAS_IO_CACHE
static IO::AsyncIO::IOCacheManager g_cache(4 * 1024 * 1024);  // 4 MB feature cache
#endif

FeatGenerator::FeatGenerator() = default;

std::vector<float> FeatGenerator::Generate(const std::string& key,
                                             const std::vector<float>& signal) const {
#if HAS_IO_CACHE
    if (cache_enabled_) {
        std::vector<uint8_t> cached;
        if (g_cache.Get(key, cached) && cached.size() % sizeof(float) == 0) {
            std::vector<float> result(cached.size() / sizeof(float));
            std::memcpy(result.data(), cached.data(), cached.size());
            return result;
        }
    }
#endif

    auto stats = extractor_.ExtractStats(signal);
    auto hist  = extractor_.ExtractHistogram(signal, 16);
    auto delta = extractor_.ExtractDelta(signal);
    auto flat  = FeatExtractor::Flatten({stats, hist, delta});

#if HAS_IO_CACHE
    if (cache_enabled_ && !flat.empty()) {
        std::vector<uint8_t> bytes(flat.size() * sizeof(float));
        std::memcpy(bytes.data(), flat.data(), bytes.size());
        g_cache.Put(key, std::move(bytes));
    }
#endif

    return flat;
}

void FeatGenerator::ClearCache() {
#if HAS_IO_CACHE
    g_cache.Clear();
#endif
}

}  // namespace XER::ML::Features
