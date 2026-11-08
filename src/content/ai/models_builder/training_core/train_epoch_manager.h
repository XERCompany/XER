#pragma once

namespace XER::ModelsBuilder::Training {

/// @brief Tracks training epochs and per-epoch metrics
class EpochManager {
 public:
  explicit EpochManager(int total_epochs);

  bool  HasNext()          const { return current_ < total_; }
  int   CurrentEpoch()     const { return current_; }
  float LastLoss()         const { return last_loss_; }

  /// Advance to next epoch and record loss
  void  Advance(float epoch_loss);

 private:
  int   total_;
  int   current_{0};
  float last_loss_{0.0f};
};

}  // namespace XER::ModelsBuilder::Training
