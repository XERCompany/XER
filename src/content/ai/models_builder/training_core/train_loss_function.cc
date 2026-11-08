#include "train_loss_function.h"

#include <algorithm>
#include <numeric>

namespace XER::ModelsBuilder::Training {

float TrainLossFunction::MseLoss(const std::vector<float>& pred,
                                   const std::vector<float>& target) {
  if (pred.size() != target.size() || pred.empty()) return 0.0f;
  float sum = 0.0f;
  for (size_t i = 0; i < pred.size(); ++i) {
    float d = pred[i] - target[i];
    sum += d * d;
  }
  return sum / static_cast<float>(pred.size());
}

float TrainLossFunction::BceLoss(const std::vector<float>& pred,
                                   const std::vector<float>& target,
                                   float eps) {
  if (pred.size() != target.size() || pred.empty()) return 0.0f;
  float sum = 0.0f;
  for (size_t i = 0; i < pred.size(); ++i) {
    float p = std::max(eps, std::min(1.0f - eps, pred[i]));
    sum += -(target[i] * std::log(p) + (1.0f - target[i]) * std::log(1.0f - p));
  }
  return sum / static_cast<float>(pred.size());
}

float TrainLossFunction::CrossEntropyLoss(const std::vector<float>& logits,
                                            const std::vector<int>& labels,
                                            int num_classes) {
  if (labels.empty() || logits.empty()) return 0.0f;
  size_t n = labels.size();
  float sum = 0.0f;
  for (size_t i = 0; i < n; ++i) {
    // Log-sum-exp
    const float* row = logits.data() + i * num_classes;
    float maxv = *std::max_element(row, row + num_classes);
    float lse = 0.0f;
    for (int c = 0; c < num_classes; ++c)
      lse += std::exp(row[c] - maxv);
    lse = std::log(lse) + maxv;
    sum += lse - row[labels[i]];
  }
  return sum / static_cast<float>(n);
}

}  // namespace XER::ModelsBuilder::Training
