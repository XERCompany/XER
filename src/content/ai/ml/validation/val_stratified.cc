#include "val_stratified.h"
#include <algorithm>
#include <numeric>
#include <random>

namespace XER::ML::Validation {

std::vector<ValStratified::FoldIndices> ValStratified::Split(
        const std::vector<int>& labels) const {
    // Bucket indices by class
    std::map<int, std::vector<size_t>> buckets;
    for (size_t i = 0; i < labels.size(); ++i)
        buckets[labels[i]].push_back(i);

    std::mt19937 rng(seed_);
    for (auto& [cls, idxs] : buckets)
        std::shuffle(idxs.begin(), idxs.end(), rng);

    std::vector<FoldIndices> folds(k_);

    // Distribute each class across k folds
    for (auto& [cls, idxs] : buckets) {
        for (size_t i = 0; i < idxs.size(); ++i)
            folds[i % static_cast<size_t>(k_)].val.push_back(idxs[i]);
    }

    // Build train = complement of val
    const size_t n = labels.size();
    for (int fold = 0; fold < k_; ++fold) {
        std::vector<bool> is_val(n, false);
        for (size_t idx : folds[fold].val) is_val[idx] = true;
        for (size_t i = 0; i < n; ++i)
            if (!is_val[i]) folds[fold].train.push_back(i);
    }
    return folds;
}

}  // namespace XER::ML::Validation
