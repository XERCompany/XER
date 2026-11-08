#pragma once

#include <vector>

namespace XER::ModelsBuilder::Training {

/// @brief Backpropagation orchestrator (forward/backward passes)
class TrainBackprop {
 public:
  /// Placeholder: accumulates gradient magnitudes for monitoring
  struct BackpropResult {
    float loss{0.0f};
    std::vector<float> grad_norms;
  };

  static BackpropResult Forward(const std::vector<float>& x,
                                  const std::vector<float>& y);
};

}  // namespace XER::ModelsBuilder::Training
