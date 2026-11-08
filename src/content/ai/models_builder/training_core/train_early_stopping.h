#pragma once

namespace XER::ModelsBuilder::Training {

/// @brief Patience-based early stopping
class EarlyStopping {
 public:
  explicit EarlyStopping(int patience = 5, float min_delta = 1e-4f);

  /// Call after each epoch with the monitored metric (e.g. val_loss)
  /// @return true if training should stop
  bool ShouldStop(float current_metric);

  int Counter()  const { return counter_; }
  float BestVal() const { return best_; }

 private:
  int   patience_;
  float min_delta_;
  float best_;
  int   counter_{0};
};

}  // namespace XER::ModelsBuilder::Training
