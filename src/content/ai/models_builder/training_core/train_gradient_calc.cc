#include "train_gradient_calc.h"

#include <functional>

namespace XER::ModelsBuilder::Training {

std::vector<float> TrainGradientCalc::Compute(
    const std::vector<float>& params,
    std::function<float(const std::vector<float>&)> loss_fn,
    float h) {
  std::vector<float> grads(params.size(), 0.0f);
  auto p = params;
  for (size_t i = 0; i < params.size(); ++i) {
    p[i] = params[i] + h;
    float fp = loss_fn(p);
    p[i] = params[i] - h;
    float fm = loss_fn(p);
    p[i] = params[i];
    grads[i] = (fp - fm) / (2.0f * h);
  }
  return grads;
}

}  // namespace XER::ModelsBuilder::Training
