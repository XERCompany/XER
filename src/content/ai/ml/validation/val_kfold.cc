#include "val_kfold.h"
#include <algorithm>
#include <numeric>
#include <random>

namespace XER::ML::Validation {

std::vector<ValKFold::FoldIndices> ValKFold::Split(size_t n_samples) const {
    std::vector<size_t> idx(n_samples);
    std::iota(idx.begin(), idx.end(), 0);
    std::mt19937 rng(seed_);
    std::shuffle(idx.begin(), idx.end(), rng);

    std::vector<FoldIndices> folds(k_);
    for (int fold = 0; fold < k_; ++fold) {
        size_t start = (n_samples * static_cast<size_t>(fold)) / static_cast<size_t>(k_);
        size_t end   = (n_samples * static_cast<size_t>(fold + 1)) / static_cast<size_t>(k_);
        folds[fold].val = std::vector<size_t>(idx.begin() + start, idx.begin() + end);
        for (size_t i = 0; i < n_samples; ++i)
            if (i < start || i >= end) folds[fold].train.push_back(idx[i]);
    }
    return folds;
}

}  // namespace XER::ML::Validation
