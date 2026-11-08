#pragma once

#include "val_kfold.h"
#include <map>
#include <vector>

namespace XER::ML::Validation {

/// Stratified K-Fold — preserves class distribution in each fold
class ValStratified {
public:
    explicit ValStratified(int k = 5, unsigned int seed = 42)
        : k_(k), seed_(seed) {}

    using FoldIndices = ValKFold::FoldIndices;

    /// Split n_samples with given integer labels into k stratified folds
    std::vector<FoldIndices> Split(const std::vector<int>& labels) const;

    int NumFolds() const { return k_; }

private:
    int k_;
    unsigned int seed_;
};

}  // namespace XER::ML::Validation
