#pragma once

#include <Eigen/Dense>
#include <vector>

namespace XER::ModelsBuilder::Optimization {

/// @brief Magnitude-based unstructured weight pruning
class OptPruningStrategy {
 public:
  /// Zero out weights below threshold (fraction of max-magnitude)
  /// @param sparsity  Target sparsity ratio [0, 1)
  static void Prune(Eigen::MatrixXf& weights, float sparsity);

  /// Count zero elements / total
  static float Sparsity(const Eigen::MatrixXf& weights);
};

}  // namespace XER::ModelsBuilder::Optimization
