#pragma once

#include <vector>
#include <cmath>

namespace XER::ModelsBuilder::Training {

/// @brief Stateless loss function utilities
class TrainLossFunction {
 public:
  /// Mean Squared Error: mean((pred - target)^2)
  static float MseLoss(const std::vector<float>& pred,
                        const std::vector<float>& target);

  /// Binary Cross Entropy
  static float BceLoss(const std::vector<float>& pred,
                        const std::vector<float>& target,
                        float eps = 1e-7f);

  /// Categorical Cross Entropy (one-hot targets flattened)
  static float CrossEntropyLoss(const std::vector<float>& logits,
                                  const std::vector<int>& labels,
                                  int num_classes);
};

}  // namespace XER::ModelsBuilder::Training
