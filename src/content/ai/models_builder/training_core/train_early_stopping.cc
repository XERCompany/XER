#include "train_early_stopping.h"

#include <limits>

namespace XER::ModelsBuilder::Training {

EarlyStopping::EarlyStopping(int patience, float min_delta)
    : patience_(patience), min_delta_(min_delta),
      best_(std::numeric_limits<float>::max()) {}

bool EarlyStopping::ShouldStop(float current_metric) {
  if (current_metric < best_ - min_delta_) {
    best_    = current_metric;
    counter_ = 0;
    return false;
  }
  ++counter_;
  return counter_ >= patience_;
}

}  // namespace XER::ModelsBuilder::Training
