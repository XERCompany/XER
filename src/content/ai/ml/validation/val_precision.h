#pragma once

#include <vector>

namespace XER::ML::Validation {

/// Precision = TP / (TP + FP), per-class and macro-averaged
struct ValPrecision {
    /// Binary precision
    static float Compute(const std::vector<int>& predictions,
                          const std::vector<int>& ground_truth,
                          int positive_class = 1);

    /// Macro-averaged precision across all classes
    static float MacroAverage(const std::vector<int>& predictions,
                               const std::vector<int>& ground_truth);
};

}  // namespace XER::ML::Validation
