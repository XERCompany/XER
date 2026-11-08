#pragma once
#include "feat_importance.h"
#include "feat_math_ops.h"

#include <cstddef>
#include <vector>

namespace XER::ML::Features {

class FeatSelection {
public:
    // Select top-k features from X by variance
    std::vector<float> SelectTopK(const std::vector<float>& X,
                                   size_t n_samples, size_t n_features,
                                   size_t k) const;

    // Filter features by variance threshold (keep features with var > threshold)
    std::vector<float> FilterByVariance(const std::vector<float>& X,
                                         size_t n_samples, size_t n_features,
                                         float threshold,
                                         std::vector<size_t>* kept_indices = nullptr) const;

    // Return selected feature indices (fitted by SelectTopK or FilterByVariance)
    const std::vector<size_t>& SelectedIndices() const { return selected_indices_; }

private:
    mutable std::vector<size_t> selected_indices_;
};

}  // namespace XER::ML::Features
