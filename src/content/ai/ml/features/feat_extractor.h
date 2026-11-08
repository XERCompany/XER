#pragma once

#include "feat_math_ops.h"

#include <cstddef>
#include <string>
#include <vector>

namespace XER::ML::Features {

struct FeatureResult {
    std::string name;
    std::vector<float> values;
    size_t n_features = 0;
};

class FeatExtractor {
public:
    FeatExtractor() = default;

    // Extract statistical features: mean, variance, min, max, l2-norm per window
    FeatureResult ExtractStats(const std::vector<float>& signal, size_t window_size = 0) const;

    // Extract histogram features
    FeatureResult ExtractHistogram(const std::vector<float>& signal, int bins = 16) const;

    // Extract difference features (first-order delta)
    FeatureResult ExtractDelta(const std::vector<float>& signal) const;

    // Flatten and concatenate multiple feature results
    static std::vector<float> Flatten(const std::vector<FeatureResult>& results);
};

}  // namespace XER::ML::Features
