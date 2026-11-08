#pragma once
#include <vector>
#include <cstddef>

namespace XER::ML::Preprocessing {

/// One-hot encode integer class indices to binary row vectors
struct PreprocessOnehot {
    /// Fit: determine number of classes
    void Fit(const std::vector<int>& labels);

    /// Encode integers → one-hot matrix (n × n_classes)
    std::vector<std::vector<float>> Transform(const std::vector<int>& labels) const;

    /// Decode one-hot rows → class indices (argmax)
    std::vector<int> InverseTransform(const std::vector<std::vector<float>>& encoded) const;

    bool IsFitted() const { return fitted_; }
    size_t NumClasses() const { return n_classes_; }

private:
    size_t n_classes_ = 0;
    bool fitted_ = false;
};

}  // namespace XER::ML::Preprocessing
