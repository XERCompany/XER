#pragma once

#include "../model_core/model.h"
#include "../model_optimization/optimizer.h"
#include "../model_optimization/loss_function.h"
#include <functional>
#include <memory>
#include <vector>

namespace XER::ModelsBuilder::Training {

struct TrainingConfig {
    uint32_t epochs;
    uint32_t batch_size;
    float learning_rate;
    bool shuffle_data;
    bool verbose;
    uint32_t early_stopping_patience = 0;
    float early_stopping_min_delta = 0.0f;
    bool enable_backend_acceleration = true;
    bool enable_parallel_batches = true;
};

using TrainingCallback = std::function<void(uint32_t, float)>;

class Trainer {
public:
    explicit Trainer(std::shared_ptr<Core::Model> model);
    
    void SetOptimizer(std::shared_ptr<Optimization::Optimizer> optimizer);
    void SetLossFunction(std::shared_ptr<Optimization::LossFunction> loss);
    
    bool Train(const std::vector<float>& x_train, const std::vector<float>& y_train,
               const TrainingConfig& config);
    
    void SetCallback(TrainingCallback callback) { callback_ = callback; }
    const std::vector<float>& GetEpochLosses() const { return epoch_losses_; }
    float GetBestLoss() const { return best_loss_; }

private:
    std::shared_ptr<Core::Model> model_;
    std::shared_ptr<Optimization::Optimizer> optimizer_;
    std::shared_ptr<Optimization::LossFunction> loss_;
    TrainingCallback callback_;
    std::vector<float> epoch_losses_;
    float best_loss_ = 0.0f;
};

} // namespace XER::ModelsBuilder::Training
