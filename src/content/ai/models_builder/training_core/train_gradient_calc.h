#pragma once

#include <functional>
#include <vector>

namespace XER::ModelsBuilder::Training {

/// @brief Computes gradients via finite difference (placeholder)
///
/// Real gradient computation requires autograd integration with the
/// CUDA ops layer. This provides a CPU fallback for small models.
class TrainGradientCalc {
 public:
  /// Finite-difference gradient: (f(x+h)-f(x-h))/(2h)
  static std::vector<float> Compute(
      const std::vector<float>& params,
      std::function<float(const std::vector<float>&)> loss_fn,
      float h = 1e-5f);
};

}  // namespace XER::ModelsBuilder::Training
