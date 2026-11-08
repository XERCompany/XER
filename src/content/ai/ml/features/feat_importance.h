#pragma once
#include "feat_math_ops.h"
#include <string>
#include <vector>

namespace XER::ML::Features {

struct ImportanceScore {
    size_t feature_index = 0;
    float score = 0.0f;
    std::string method;
};

class FeatImportance {
public:
    // Variance-based importance: higher variance → more discriminative
    std::vector<ImportanceScore> ByVariance(const std::vector<float>& X,
                                             size_t n_samples, size_t n_features) const;

    // Correlation-based importance with respect to a target vector
    std::vector<ImportanceScore> ByCorrelation(const std::vector<float>& X,
                                                size_t n_samples, size_t n_features,
                                                const std::vector<float>& y) const;

    // Return top-k feature indices sorted by score (descending)
    static std::vector<size_t> TopK(const std::vector<ImportanceScore>& scores, size_t k);
};

}  // namespace XER::ML::Features
