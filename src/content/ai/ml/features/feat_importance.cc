#include "feat_importance.h"
#include <algorithm>
#include <cmath>

namespace XER::ML::Features {

std::vector<ImportanceScore> FeatImportance::ByVariance(
    const std::vector<float>& X, size_t n_samples, size_t n_features) const {
    std::vector<ImportanceScore> scores(n_features);
    for (size_t j = 0; j < n_features; ++j) {
        std::vector<float> col(n_samples);
        for (size_t i = 0; i < n_samples; ++i)
            col[i] = X[i * n_features + j];
        scores[j].feature_index = j;
        scores[j].score = FeatMathOps::Variance(col);
        scores[j].method = "variance";
    }
    return scores;
}

std::vector<ImportanceScore> FeatImportance::ByCorrelation(
    const std::vector<float>& X, size_t n_samples, size_t n_features,
    const std::vector<float>& y) const {
    std::vector<ImportanceScore> scores(n_features);
    if (y.size() != n_samples) return scores;

    float y_mean = FeatMathOps::Mean(y);
    float y_std = std::sqrt(FeatMathOps::Variance(y));
    if (y_std < 1e-9f) return scores;

    for (size_t j = 0; j < n_features; ++j) {
        std::vector<float> col(n_samples);
        for (size_t i = 0; i < n_samples; ++i) col[i] = X[i * n_features + j];
        float x_mean = FeatMathOps::Mean(col);
        float x_std  = std::sqrt(FeatMathOps::Variance(col));
        if (x_std < 1e-9f) { scores[j] = {j, 0.0f, "correlation"}; continue; }

        float cov = 0.0f;
        for (size_t i = 0; i < n_samples; ++i)
            cov += (col[i] - x_mean) * (y[i] - y_mean);
        cov /= static_cast<float>(n_samples);
        scores[j] = {j, std::abs(cov / (x_std * y_std)), "correlation"};
    }
    return scores;
}

std::vector<size_t> FeatImportance::TopK(const std::vector<ImportanceScore>& scores, size_t k) {
    std::vector<size_t> idx(scores.size());
    for (size_t i = 0; i < idx.size(); ++i) idx[i] = i;
    std::partial_sort(idx.begin(),
                      idx.begin() + std::min(k, idx.size()),
                      idx.end(),
                      [&scores](size_t a, size_t b) {
                          return scores[a].score > scores[b].score;
                      });
    idx.resize(std::min(k, idx.size()));
    return idx;
}

}  // namespace XER::ML::Features
