#include "feat_pca.h"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace XER::ML::Features {

std::vector<float> FeatPCA::PowerIteration(const std::vector<float>& A, size_t n, int iters) {
    std::vector<float> v(n, 1.0f / std::sqrt(static_cast<float>(n)));
    for (int it = 0; it < iters; ++it) {
        // Av
        std::vector<float> av(n, 0.0f);
        for (size_t i = 0; i < n; ++i)
            for (size_t j = 0; j < n; ++j)
                av[i] += A[i * n + j] * v[j];
        float norm = FeatMathOps::L2Norm(av);
        if (norm < 1e-9f) break;
        for (size_t i = 0; i < n; ++i) v[i] = av[i] / norm;
    }
    return v;
}

bool FeatPCA::Fit(const std::vector<float>& X, size_t n_samples, size_t n_features,
                   size_t n_components) {
    if (n_components > n_features) n_components = n_features;
    n_components_ = n_components;
    n_features_   = n_features;
    fitted_       = false;

    // Center X
    mean_.resize(n_features, 0.0f);
    for (size_t i = 0; i < n_samples; ++i)
        for (size_t j = 0; j < n_features; ++j)
            mean_[j] += X[i * n_features + j];
    for (auto& m : mean_) m /= static_cast<float>(n_samples);

    std::vector<float> Xc(X.size());
    for (size_t i = 0; i < n_samples; ++i)
        for (size_t j = 0; j < n_features; ++j)
            Xc[i * n_features + j] = X[i * n_features + j] - mean_[j];

    // Covariance matrix (n_features × n_features)
    auto C = FeatMathOps::CovarianceMatrix(Xc, n_samples, n_features);

    // Deflation: extract n_components eigenvectors via power iteration
    components_.resize(n_components * n_features);
    explained_variance_ratio_.resize(n_components, 0.0f);

    float total_var = 0.0f;
    for (size_t j = 0; j < n_features; ++j) total_var += C[j * n_features + j];

    std::vector<float> Cdefl = C;
    for (size_t k = 0; k < n_components; ++k) {
        auto v = PowerIteration(Cdefl, n_features);
        // eigenvalue ≈ vᵀCv
        float eigen = 0.0f;
        for (size_t i = 0; i < n_features; ++i)
            for (size_t j = 0; j < n_features; ++j)
                eigen += v[i] * Cdefl[i * n_features + j] * v[j];

        for (size_t j = 0; j < n_features; ++j)
            components_[k * n_features + j] = v[j];
        explained_variance_ratio_[k] = (total_var > 1e-9f) ? (eigen / total_var) : 0.0f;

        // Deflate: C -= eigen * v*vᵀ
        for (size_t i = 0; i < n_features; ++i)
            for (size_t j = 0; j < n_features; ++j)
                Cdefl[i * n_features + j] -= eigen * v[i] * v[j];
    }

    fitted_ = true;
    return true;
}

std::vector<float> FeatPCA::Transform(const std::vector<float>& X,
                                       size_t n_samples, size_t n_features) const {
    if (!fitted_) return {};
    // Center
    std::vector<float> Xc(X.size());
    for (size_t i = 0; i < n_samples; ++i)
        for (size_t j = 0; j < n_features; ++j)
            Xc[i * n_features + j] = X[i * n_features + j] - mean_[j];
    // Project: Xc (n×f) × Wᵀ (f×k) → out (n×k)
    return FeatMathOps::MatMul(Xc, n_samples, n_features, components_, n_components_);
}

std::vector<float> FeatPCA::FitTransform(const std::vector<float>& X,
                                          size_t n_samples, size_t n_features,
                                          size_t n_components) {
    Fit(X, n_samples, n_features, n_components);
    return Transform(X, n_samples, n_features);
}

}  // namespace XER::ML::Features
