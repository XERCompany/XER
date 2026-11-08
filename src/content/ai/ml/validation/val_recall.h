#pragma once

#include <vector>

namespace XER::ML::Validation {

/// Recall = TP / (TP + FN), per-class and macro-averaged
struct ValRecall {
    static float Compute(const std::vector<int>& predictions,
                          const std::vector<int>& ground_truth,
                          int positive_class = 1);

    static float MacroAverage(const std::vector<int>& predictions,
                               const std::vector<int>& ground_truth);
};

}  // namespace XER::ML::Validation
