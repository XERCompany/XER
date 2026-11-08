#pragma once
#include "feat_math_ops.h"

#include <cstddef>
#include <vector>

namespace XER::ML::Features {

class FeatLDA {
public:
    // Fit LDA for binary classification.
    // X: n_samples × n_features row-major; labels: binary (0 or 1)
    bool Fit(const std::vector<float>& X, size_t n_samples, size_t n_features,
             const std::vector<int>& labels);

    // Project onto LDA direction(s)
    std::vector<float> Transform(const std::vector<float>& X,
                                  size_t n_samples, size_t n_features) const;

    bool IsFitted() const { return fitted_; }

private:
    std::vector<float> direction_;  // LDA projection vector (n_features)
    std::vector<float> class_means_[2];
    bool fitted_ = false;
    size_t n_features_ = 0;
};

}  // namespace XER::ML::Features
