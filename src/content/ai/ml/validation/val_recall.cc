#include "val_recall.h"
#include <set>

namespace XER::ML::Validation {

float ValRecall::Compute(const std::vector<int>& predictions,
                          const std::vector<int>& ground_truth,
                          int positive_class) {
    int tp = 0, fn = 0;
    for (size_t i = 0; i < predictions.size() && i < ground_truth.size(); ++i) {
        if (ground_truth[i] == positive_class) {
            if (predictions[i] == positive_class) ++tp;
            else ++fn;
        }
    }
    return (tp + fn > 0) ? static_cast<float>(tp) / static_cast<float>(tp + fn) : 0.0f;
}

float ValRecall::MacroAverage(const std::vector<int>& predictions,
                               const std::vector<int>& ground_truth) {
    std::set<int> classes(ground_truth.begin(), ground_truth.end());
    if (classes.empty()) return 0.0f;
    float sum = 0.0f;
    for (int c : classes) sum += Compute(predictions, ground_truth, c);
    return sum / static_cast<float>(classes.size());
}

}  // namespace XER::ML::Validation
