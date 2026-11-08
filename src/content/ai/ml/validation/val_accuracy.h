#pragma once

#include <cstddef>
#include <vector>

namespace XER::ML::Validation {

/// Accuracy score: (TP + TN) / N
struct ValAccuracy {
    /// Compute accuracy from integer class predictions and ground-truth labels.
    static float Compute(const std::vector<int>& predictions,
                          const std::vector<int>& ground_truth);

    /// Top-k accuracy (multi-class: check if true label is in top-k)
    /// probs: n_samples × n_classes row-major float probabilities
    static float TopK(const std::vector<float>& probs, size_t n_classes,
                       const std::vector<int>& ground_truth, int k = 5);
};

}  // namespace XER::ML::Validation
