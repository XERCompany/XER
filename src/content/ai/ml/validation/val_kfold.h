#pragma once

#include <cstddef>
#include <vector>

namespace XER::ML::Validation {

/// K-Fold cross-validation index splitter
class ValKFold {
public:
    explicit ValKFold(int k = 5, unsigned int seed = 42)
        : k_(k), seed_(seed) {}

    struct FoldIndices {
        std::vector<size_t> train;
        std::vector<size_t> val;
    };

    /// Generate all k folds for n_samples
    std::vector<FoldIndices> Split(size_t n_samples) const;

    int NumFolds() const { return k_; }

private:
    int k_;
    unsigned int seed_;
};

}  // namespace XER::ML::Validation
