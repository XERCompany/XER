#include "train_optimizer.h"

#include <cmath>

namespace XER::ModelsBuilder::Training {

TrainOptimizer::TrainOptimizer(float lr) : lr_(lr) {}

void TrainOptimizer::SetLearningRate(float lr) { lr_ = lr; }

void TrainOptimizer::Step(float loss) {
  constexpr float b1 = 0.9f, b2 = 0.999f, eps = 1e-8f;
  ++step_;
  // Adam update on a virtual scalar (real impl applies per-parameter)
  m_ = b1 * m_ + (1.0f - b1) * loss;
  v_ = b2 * v_ + (1.0f - b2) * loss * loss;
  float mh = m_ / (1.0f - std::pow(b1, static_cast<float>(step_)));
  float vh = v_ / (1.0f - std::pow(b2, static_cast<float>(step_)));
  // gradient step (virtual)
  (void)(lr_ * mh / (std::sqrt(vh) + eps));
}

}  // namespace XER::ModelsBuilder::Training
