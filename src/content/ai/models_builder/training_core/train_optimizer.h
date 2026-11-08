#pragma once

namespace XER::ModelsBuilder::Training {

/// @brief Gradient-based parameter optimizer (SGD + Adam)
class TrainOptimizer {
 public:
  explicit TrainOptimizer(float lr = 1e-3f);

  /// SGD update step (placeholder: updates virtual param)
  void Step(float loss);

  void SetLearningRate(float lr);
  float GetLearningRate() const { return lr_; }

 private:
  float lr_;
  int   step_{0};
  // Adam state
  float m_{0.0f}, v_{0.0f};
};

}  // namespace XER::ModelsBuilder::Training
