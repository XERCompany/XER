#include "train_scheduler.h"

#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif
#include <cmath>

namespace XER::ModelsBuilder::Training {

TrainScheduler::TrainScheduler(Config config) : config_(config) {}

float TrainScheduler::GetLr(int epoch) const {
  switch (config_.type) {
    case ScheduleType::Constant:
      return config_.lr;

    case ScheduleType::StepDecay: {
      int steps = epoch / config_.step_size;
      float lr = config_.lr;
      for (int i = 0; i < steps; ++i) lr *= config_.gamma;
      return lr;
    }

    case ScheduleType::ExponentialDecay:
      return config_.lr * std::pow(config_.gamma, static_cast<float>(epoch));

    case ScheduleType::CosineAnnealing: {
      float t = static_cast<float>(epoch % config_.t_max);
      float T = static_cast<float>(config_.t_max);
      return config_.eta_min +
             0.5f * (config_.lr - config_.eta_min) *
             (1.0f + std::cos(static_cast<float>(M_PI) * t / T));
    }
  }
  return config_.lr;
}

}  // namespace XER::ModelsBuilder::Training
