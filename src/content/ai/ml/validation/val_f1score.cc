#include "val_f1score.h"
#include "val_precision.h"
#include "val_recall.h"
#include <map>
#include <set>

namespace XER::ML::Validation {

float ValF1Score::Compute(const std::vector<int>& predictions,
                           const std::vector<int>& ground_truth,
                           int positive_class) {
    float p = ValPrecision::Compute(predictions, ground_truth, positive_class);
    float r = ValRecall::Compute(predictions, ground_truth, positive_class);
    return (p + r > 0.0f) ? 2.0f * p * r / (p + r) : 0.0f;
}

float ValF1Score::MacroAverage(const std::vector<int>& predictions,
                                const std::vector<int>& ground_truth) {
    std::set<int> classes(ground_truth.begin(), ground_truth.end());
    if (classes.empty()) return 0.0f;
    float sum = 0.0f;
    for (int c : classes) sum += Compute(predictions, ground_truth, c);
    return sum / static_cast<float>(classes.size());
}

float ValF1Score::WeightedAverage(const std::vector<int>& predictions,
                                   const std::vector<int>& ground_truth) {
    std::map<int, int> support;
    for (int lbl : ground_truth) ++support[lbl];
    const float total = static_cast<float>(ground_truth.size());
    if (total == 0.0f) return 0.0f;
    float sum = 0.0f;
    for (auto& [c, cnt] : support)
        sum += Compute(predictions, ground_truth, c) * static_cast<float>(cnt);
    return sum / total;
}

}  // namespace XER::ML::Validation
