#pragma once

#include <cstddef>
#include <utility>
#include <vector>

namespace XER::ML::Validation {

/// Train/test split utilities
struct ValSplit {
    struct SplitResult {
        std::vector<size_t> train_indices;
        std::vector<size_t> test_indices;
    };

    /// Random train/test split (shuffle then cut)
    static SplitResult Split(size_t n_samples, float test_ratio = 0.2f,
                              unsigned int seed = 42);

    /// Split X matrix (n_samples × n_features, row-major)
    static std::pair<std::vector<float>, std::vector<float>>
    SplitX(const std::vector<float>& X, size_t n_samples, size_t n_features,
            float test_ratio = 0.2f, unsigned int seed = 42);

    /// Split labels vector
    static std::pair<std::vector<int>, std::vector<int>>
    SplitY(const std::vector<int>& y, float test_ratio = 0.2f,
            unsigned int seed = 42);
};

}  // namespace XER::ML::Validation
