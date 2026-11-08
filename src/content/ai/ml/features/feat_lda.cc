#include "feat_lda.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace XER::ML::Features {

bool FeatLDA::Fit(const std::vector<float>& X, size_t n_samples, size_t n_features,
                   const std::vector<int>& labels) {
    if (labels.size() != n_samples) return false;
    n_features_ = n_features;
    fitted_ = false;

    // Compute per-class means
    for (int c = 0; c < 2; ++c) {
        class_means_[c].assign(n_features, 0.0f);
    }
    std::vector<size_t> counts(2, 0);
    for (size_t i = 0; i < n_samples; ++i) {
        int lbl = std::clamp(labels[i], 0, 1);
        for (size_t j = 0; j < n_features; ++j)
            class_means_[lbl][j] += X[i * n_features + j];
        counts[lbl]++;
    }
    for (int c = 0; c < 2; ++c) {
        if (counts[c] > 0)
            for (auto& v : class_means_[c]) v /= static_cast<float>(counts[c]);
    }

    // Within-class scatter Sw
    std::vector<float> Sw(n_features * n_features, 0.0f);
    for (size_t i = 0; i < n_samples; ++i) {
        int lbl = std::clamp(labels[i], 0, 1);
        for (size_t r = 0; r < n_features; ++r)
            for (size_t c2 = 0; c2 < n_features; ++c2)
                Sw[r * n_features + c2] +=
                    (X[i * n_features + r] - class_means_[lbl][r]) *
                    (X[i * n_features + c2] - class_means_[lbl][c2]);
    }

    // Add small regularization to Sw diagonal
    for (size_t j = 0; j < n_features; ++j)
        Sw[j * n_features + j] += 1e-6f;

    // LDA direction ∝ Sw⁻¹ * (μ1 - μ0)
    // Simple approach: solve Sw * w = (μ1 - μ0) via diagonal approximation
    auto diff = FeatMathOps::Subtract(class_means_[1], class_means_[0]);
    direction_.resize(n_features);
    for (size_t j = 0; j < n_features; ++j) {
        float sw_diag = Sw[j * n_features + j];
        direction_[j] = (sw_diag > 1e-9f) ? (diff[j] / sw_diag) : diff[j];
    }
    direction_ = FeatMathOps::Normalize(direction_);
    fitted_ = true;
    return true;
}

std::vector<float> FeatLDA::Transform(const std::vector<float>& X,
                                       size_t n_samples, size_t n_features) const {
    if (!fitted_) return {};
    std::vector<float> out(n_samples);
    for (size_t i = 0; i < n_samples; ++i) {
        float proj = 0.0f;
        for (size_t j = 0; j < n_features && j < direction_.size(); ++j)
            proj += X[i * n_features + j] * direction_[j];
        out[i] = proj;
    }
    return out;
}

}  // namespace XER::ML::Features
