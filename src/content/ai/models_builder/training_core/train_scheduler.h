#pragma once

namespace XER::ModelsBuilder::Training {

/// @brief Learning rate scheduler
class TrainScheduler {
 public:
  enum class ScheduleType {
    Constant,          ///< Fixed LR
    StepDecay,         ///< Multiply by gamma every step_size epochs
    CosineAnnealing,   ///< Cosine LR schedule
    ExponentialDecay,  ///< lr *= gamma each epoch
  };

  struct Config {
    ScheduleType type{ScheduleType::Constant};
    float lr{1e-3f};
    float gamma{0.1f};
    int   step_size{10};
    int   t_max{100};    ///< for cosine
    float eta_min{0.0f};
  };

  explicit TrainScheduler(Config config);

  /// @return Learning rate for epoch (0-based)
  float GetLr(int epoch) const;

 private:
  Config config_;
};

}  // namespace XER::ModelsBuilder::Training
