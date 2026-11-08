#include "opt_pruning_strategy.h"

#include <algorithm>
#include <vector>

namespace XER::ModelsBuilder::Optimization {

void OptPruningStrategy::Prune(Eigen::MatrixXf& weights, float sparsity) {
  // Collect magnitudes, find threshold via percentile
  std::vector<float> vals(weights.data(),
                           weights.data() + weights.size());
  std::transform(vals.begin(), vals.end(), vals.begin(),
                  [](float v) { return std::abs(v); });
  std::sort(vals.begin(), vals.end());

  size_t idx = static_cast<size_t>(sparsity * static_cast<float>(vals.size()));
  if (idx >= vals.size()) idx = vals.size() - 1;
  float threshold = vals[idx];

  for (int i = 0; i < weights.rows(); ++i)
    for (int j = 0; j < weights.cols(); ++j)
      if (std::abs(weights(i, j)) <= threshold)
        weights(i, j) = 0.0f;
}

float OptPruningStrategy::Sparsity(const Eigen::MatrixXf& weights) {
  float zeros = (weights.array() == 0.0f).cast<float>().sum();
  return zeros / static_cast<float>(weights.size());
}

}  // namespace XER::ModelsBuilder::Optimization
