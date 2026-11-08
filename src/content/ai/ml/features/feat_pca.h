#pragma once
#include "feat_math_ops.h"

#include <cstddef>
#include <vector>

namespace XER::ML::Features {

class FeatPCA {
public:
    // Fit PCA on X (n_samples × n_features) keeping n_components principal components.
    bool Fit(const std::vector<float>& X, size_t n_samples, size_t n_features,
             size_t n_components);

    // Project new data onto the fitted components.
    std::vector<float> Transform(const std::vector<float>& X,
                                  size_t n_samples, size_t n_features) const;

    // Fit then transform.
    std::vector<float> FitTransform(const std::vector<float>& X,
                                     size_t n_samples, size_t n_features,
                                     size_t n_components);

    // Explained variance ratio per component
    const std::vector<float>& ExplainedVarianceRatio() const { return explained_variance_ratio_; }
    size_t NumComponents() const { return n_components_; }
    bool IsFitted() const { return fitted_; }

private:
    std::vector<float> components_;         // n_components × n_features, row-major
    std::vector<float> mean_;               // n_features
    std::vector<float> explained_variance_ratio_;
    size_t n_components_ = 0;
    size_t n_features_ = 0;
    bool fitted_ = false;

    // Power iteration to find dominant eigenvector of symmetric matrix A (n×n)
    static std::vector<float> PowerIteration(const std::vector<float>& A, size_t n, int iters = 200);
};

}  // namespace XER::ML::Features
