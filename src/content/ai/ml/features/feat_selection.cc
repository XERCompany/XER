#include "feat_selection.h"
#include <algorithm>
#include <cmath>

namespace XER::ML::Features {

std::vector<float> FeatSelection::SelectTopK(const std::vector<float>& X,
                                               size_t n_samples, size_t n_features,
                                               size_t k) const {
    FeatImportance imp;
    auto scores = imp.ByVariance(X, n_samples, n_features);
    selected_indices_ = FeatImportance::TopK(scores, k);

    const size_t actual_k = selected_indices_.size();
    std::vector<float> out(n_samples * actual_k);
    for (size_t i = 0; i < n_samples; ++i)
        for (size_t j = 0; j < actual_k; ++j)
            out[i * actual_k + j] = X[i * n_features + selected_indices_[j]];
    return out;
}

std::vector<float> FeatSelection::FilterByVariance(const std::vector<float>& X,
                                                    size_t n_samples, size_t n_features,
                                                    float threshold,
                                                    std::vector<size_t>* kept_indices) const {
    selected_indices_.clear();
    for (size_t j = 0; j < n_features; ++j) {
        std::vector<float> col(n_samples);
        for (size_t i = 0; i < n_samples; ++i) col[i] = X[i * n_features + j];
        if (FeatMathOps::Variance(col) > threshold)
            selected_indices_.push_back(j);
    }

    const size_t k = selected_indices_.size();
    std::vector<float> out(n_samples * k);
    for (size_t i = 0; i < n_samples; ++i)
        for (size_t j = 0; j < k; ++j)
            out[i * k + j] = X[i * n_features + selected_indices_[j]];

    if (kept_indices) *kept_indices = selected_indices_;
    return out;
}

}  // namespace XER::ML::Features
