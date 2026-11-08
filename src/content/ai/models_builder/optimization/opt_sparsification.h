#pragma once

#include <Eigen/Dense>
#include <Eigen/Sparse>

namespace XER::ModelsBuilder::Optimization {

/// @brief Creates and manages sparse representations of dense weight matrices
class OptSparsification {
 public:
  /// Convert dense matrix to CSR-like Eigen sparse matrix
  static Eigen::SparseMatrix<float> ToDense(const Eigen::MatrixXf& dense,
                                              float threshold = 1e-6f);

  /// @return density (fraction of non-zero elements)
  static float Density(const Eigen::SparseMatrix<float>& sparse);
};

}  // namespace XER::ModelsBuilder::Optimization
