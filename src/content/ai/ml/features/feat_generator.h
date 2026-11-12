#pragma once
#include "feat_extractor.h"

#include <string>
#include <vector>

namespace XER::ML::Features {

// Generates composite feature vectors by combining multiple extraction strategies.
// Integrates with XER::AsyncIO::IO::Async::IOCacheManager via #if __has_include guard.
class FeatGenerator {
public:
    FeatGenerator();

    // Generate full feature vector from raw signal
    std::vector<float> Generate(const std::string& key,
                                 const std::vector<float>& signal) const;

    // Clear cached feature vectors
    void ClearCache();

    void SetCachingEnabled(bool enabled) { cache_enabled_ = enabled; }

private:
    FeatExtractor extractor_;
    bool cache_enabled_ = true;
};

}  // namespace XER::ML::Features
