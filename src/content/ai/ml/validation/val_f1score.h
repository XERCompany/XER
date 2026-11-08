#pragma once

#include <vector>

namespace XER::ML::Validation {

/// F1 Score = 2 * precision * recall / (precision + recall)
struct ValF1Score {
    static float Compute(const std::vector<int>& predictions,
                          const std::vector<int>& ground_truth,
                          int positive_class = 1);

    /// Macro-averaged F1 across all classes
    static float MacroAverage(const std::vector<int>& predictions,
                               const std::vector<int>& ground_truth);

    /// Weighted-averaged F1 (weight = class support / total)
    static float WeightedAverage(const std::vector<int>& predictions,
                                  const std::vector<int>& ground_truth);
};

}  // namespace XER::ML::Validation
