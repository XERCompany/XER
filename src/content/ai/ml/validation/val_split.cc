#include "val_split.h"
#include <algorithm>
#include <numeric>
#include <random>

namespace XER::ML::Validation {

ValSplit::SplitResult ValSplit::Split(size_t n_samples, float test_ratio,
                                      unsigned int seed) {
    std::vector<size_t> idx(n_samples);
    std::iota(idx.begin(), idx.end(), 0);
    std::mt19937 rng(seed);
    std::shuffle(idx.begin(), idx.end(), rng);
    const size_t n_test = static_cast<size_t>(n_samples * test_ratio);
    SplitResult r;
    r.test_indices  = std::vector<size_t>(idx.begin(), idx.begin() + n_test);
    r.train_indices = std::vector<size_t>(idx.begin() + n_test, idx.end());
    return r;
}

std::pair<std::vector<float>, std::vector<float>>
ValSplit::SplitX(const std::vector<float>& X, size_t n_samples, size_t n_features,
                  float test_ratio, unsigned int seed) {
    auto s = Split(n_samples, test_ratio, seed);
    std::vector<float> tr(s.train_indices.size() * n_features);
    std::vector<float> te(s.test_indices.size() * n_features);
    for (size_t i = 0; i < s.train_indices.size(); ++i)
        for (size_t j = 0; j < n_features; ++j)
            tr[i * n_features + j] = X[s.train_indices[i] * n_features + j];
    for (size_t i = 0; i < s.test_indices.size(); ++i)
        for (size_t j = 0; j < n_features; ++j)
            te[i * n_features + j] = X[s.test_indices[i] * n_features + j];
    return {tr, te};
}

std::pair<std::vector<int>, std::vector<int>>
ValSplit::SplitY(const std::vector<int>& y, float test_ratio, unsigned int seed) {
    auto s = Split(y.size(), test_ratio, seed);
    std::vector<int> tr, te;
    for (size_t idx : s.train_indices) tr.push_back(y[idx]);
    for (size_t idx : s.test_indices)  te.push_back(y[idx]);
    return {tr, te};
}

}  // namespace XER::ML::Validation
