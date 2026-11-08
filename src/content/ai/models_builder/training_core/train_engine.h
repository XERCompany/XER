#pragma once

#include "../model_core/model.h"
#include <functional>
#include <memory>
#include <string>

namespace XER::ModelsBuilder::Training {

struct TrainConfig {
  int   epochs{10};
  int   batch_size{32};
  float learning_rate{1e-3f};
  bool  use_cuda{false};
  int   log_every_n_steps{100};
};

/// @brief Entry point for the model training loop
class TrainEngine {
 public:
  explicit TrainEngine(std::shared_ptr<Core::Model> model,
                        TrainConfig config);

  /// Register a data callback: called per-step to supply (X, y) batch
  using DataCallback = std::function<
      std::pair<std::vector<float>, std::vector<float>>(int /*step*/)>;

  void SetDataCallback(DataCallback cb);

  /// Run full training for config.epochs
  /// @return final average loss
  float Run();

  /// Current epoch (0-based)
  int   CurrentEpoch() const { return current_epoch_; }

 private:
  std::shared_ptr<Core::Model> model_;
  TrainConfig config_;
  DataCallback data_cb_;
  int current_epoch_{0};
};

}  // namespace XER::ModelsBuilder::Training
