#include "train_engine.h"

#include "train_loss_function.h"
#include "train_optimizer.h"
#include "train_epoch_manager.h"
#include "train_early_stopping.h"

#include "../../../../error_handler/err_monitor.h"
#include "../../../../error_handler/err_diagnostic_data.h"

namespace XER::ModelsBuilder::Training {

TrainEngine::TrainEngine(std::shared_ptr<Core::Model> model,
                           TrainConfig config)
    : model_(std::move(model)), config_(std::move(config)) {}

void TrainEngine::SetDataCallback(DataCallback cb) {
  data_cb_ = std::move(cb);
}

float TrainEngine::Run() {
  if (!data_cb_) {
    XER::ErrorHandler::ReportException("TrainEngine",
        "No data callback registered");
    return -1.0f;
  }

  EpochManager epoch_mgr(config_.epochs);
  EarlyStopping early_stop(/*patience=*/5);
  TrainOptimizer optimizer(config_.learning_rate);

  float total_loss = 0.0f;
  int   total_steps = 0;

  while (epoch_mgr.HasNext()) {
    current_epoch_ = epoch_mgr.CurrentEpoch();
    float epoch_loss = 0.0f;

    // Simple single-step epoch for now; real impl uses batch processor
    auto [x, y] = data_cb_(current_epoch_);
    epoch_loss = TrainLossFunction::MseLoss(x, y);
    total_loss += epoch_loss;
    ++total_steps;

    optimizer.Step(epoch_loss);
    epoch_mgr.Advance(epoch_loss);

    if (early_stop.ShouldStop(epoch_loss)) break;
  }

  return total_steps > 0 ? total_loss / total_steps : 0.0f;
}

}  // namespace XER::ModelsBuilder::Training
