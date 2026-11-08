#include "train_backprop.h"

#include "train_loss_function.h"

namespace XER::ModelsBuilder::Training {

TrainBackprop::BackpropResult TrainBackprop::Forward(
    const std::vector<float>& x, const std::vector<float>& y) {
  BackpropResult result;
  result.loss = TrainLossFunction::MseLoss(x, y);
  // Placeholder gradient norm: ||x - y||_2 / n
  float norm = 0.0f;
  for (size_t i = 0; i < x.size() && i < y.size(); ++i) {
    float d = x[i] - y[i];
    norm += d * d;
  }
  if (!x.empty()) norm = std::sqrt(norm) / x.size();
  result.grad_norms.push_back(norm);
  return result;
}

}  // namespace XER::ModelsBuilder::Training
