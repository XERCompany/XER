#pragma once

#include <cstddef>
#include <vector>

namespace XER::ML::Features {

// Low-level math primitives used throughout ml/features.
// Optional Eigen acceleration via __has_include.
struct FeatMathOps {
    static float DotProduct(const std::vector<float>& a, const std::vector<float>& b);
    static float L2Norm(const std::vector<float>& v);
    static std::vector<float> Normalize(const std::vector<float>& v);
    static std::vector<float> Subtract(const std::vector<float>& a, const std::vector<float>& b);
    static std::vector<float> Add(const std::vector<float>& a, const std::vector<float>& b);
    static std::vector<float> Scale(const std::vector<float>& v, float s);
    static float Mean(const std::vector<float>& v);
    static float Variance(const std::vector<float>& v);

    // Matrix multiply: A(m×k) * B(k×n) → C(m×n), row-major flat
    static std::vector<float> MatMul(const std::vector<float>& A, size_t m, size_t k,
                                      const std::vector<float>& B, size_t n);

    // Covariance matrix from rows of X (n_samples × n_features), row-major
    static std::vector<float> CovarianceMatrix(const std::vector<float>& X,
                                                size_t n_samples, size_t n_features);
};

}  // namespace XER::ML::Features
