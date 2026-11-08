#include "feat_math_ops.h"
#include <cmath>
#include <numeric>
#include <stdexcept>

// Optional Eigen acceleration
#if __has_include(<Eigen/Dense>)
#  include <Eigen/Dense>
#  define HAS_EIGEN 1
#else
#  define HAS_EIGEN 0
#endif

namespace XER::ML::Features {

float FeatMathOps::DotProduct(const std::vector<float>& a, const std::vector<float>& b) {
    float s = 0.0f;
    const size_t n = std::min(a.size(), b.size());
    for (size_t i = 0; i < n; ++i) s += a[i] * b[i];
    return s;
}

float FeatMathOps::L2Norm(const std::vector<float>& v) {
    float s = 0.0f;
    for (auto x : v) s += x * x;
    return std::sqrt(s);
}

std::vector<float> FeatMathOps::Normalize(const std::vector<float>& v) {
    float n = L2Norm(v);
    if (n < 1e-9f) return v;
    return Scale(v, 1.0f / n);
}

std::vector<float> FeatMathOps::Subtract(const std::vector<float>& a, const std::vector<float>& b) {
    const size_t n = std::min(a.size(), b.size());
    std::vector<float> r(n);
    for (size_t i = 0; i < n; ++i) r[i] = a[i] - b[i];
    return r;
}

std::vector<float> FeatMathOps::Add(const std::vector<float>& a, const std::vector<float>& b) {
    const size_t n = std::min(a.size(), b.size());
    std::vector<float> r(n);
    for (size_t i = 0; i < n; ++i) r[i] = a[i] + b[i];
    return r;
}

std::vector<float> FeatMathOps::Scale(const std::vector<float>& v, float s) {
    std::vector<float> r(v.size());
    for (size_t i = 0; i < v.size(); ++i) r[i] = v[i] * s;
    return r;
}

float FeatMathOps::Mean(const std::vector<float>& v) {
    if (v.empty()) return 0.0f;
    return std::accumulate(v.begin(), v.end(), 0.0f) / v.size();
}

float FeatMathOps::Variance(const std::vector<float>& v) {
    if (v.size() < 2) return 0.0f;
    float m = Mean(v), s = 0.0f;
    for (auto x : v) s += (x - m) * (x - m);
    return s / v.size();
}

std::vector<float> FeatMathOps::MatMul(const std::vector<float>& A, size_t m, size_t k,
                                        const std::vector<float>& B, size_t n) {
#if HAS_EIGEN
    Eigen::Map<const Eigen::MatrixXf> MA(A.data(), m, k);
    Eigen::Map<const Eigen::MatrixXf> MB(B.data(), k, n);
    Eigen::MatrixXf MC = MA * MB;
    return std::vector<float>(MC.data(), MC.data() + m * n);
#else
    std::vector<float> C(m * n, 0.0f);
    for (size_t i = 0; i < m; ++i)
        for (size_t j = 0; j < n; ++j)
            for (size_t p = 0; p < k; ++p)
                C[i * n + j] += A[i * k + p] * B[p * n + j];
    return C;
#endif
}

std::vector<float> FeatMathOps::CovarianceMatrix(const std::vector<float>& X,
                                                   size_t n_samples, size_t n_features) {
    std::vector<float> means(n_features, 0.0f);
    for (size_t i = 0; i < n_samples; ++i)
        for (size_t j = 0; j < n_features; ++j)
            means[j] += X[i * n_features + j];
    for (auto& m : means) m /= static_cast<float>(n_samples);

    std::vector<float> cov(n_features * n_features, 0.0f);
    for (size_t i = 0; i < n_samples; ++i)
        for (size_t r = 0; r < n_features; ++r)
            for (size_t c = 0; c < n_features; ++c)
                cov[r * n_features + c] += (X[i * n_features + r] - means[r])
                                         * (X[i * n_features + c] - means[c]);
    const float denom = static_cast<float>(n_samples - 1 > 0 ? n_samples - 1 : 1);
    for (auto& v : cov) v /= denom;
    return cov;
}

}  // namespace XER::ML::Features
