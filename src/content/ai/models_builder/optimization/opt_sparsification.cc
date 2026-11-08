#include "opt_sparsification.h"

namespace XER::ModelsBuilder::Optimization {

Eigen::SparseMatrix<float> OptSparsification::ToDense(
    const Eigen::MatrixXf& dense, float threshold) {
  Eigen::SparseMatrix<float> sparse(dense.rows(), dense.cols());
  std::vector<Eigen::Triplet<float>> triplets;
  for (int i = 0; i < dense.rows(); ++i)
    for (int j = 0; j < dense.cols(); ++j)
      if (std::abs(dense(i, j)) > threshold)
        triplets.emplace_back(i, j, dense(i, j));
  sparse.setFromTriplets(triplets.begin(), triplets.end());
  return sparse;
}

float OptSparsification::Density(const Eigen::SparseMatrix<float>& sparse) {
  if (sparse.size() == 0) return 0.0f;
  return static_cast<float>(sparse.nonZeros()) /
         static_cast<float>(sparse.size());
}

}  // namespace XER::ModelsBuilder::Optimization
