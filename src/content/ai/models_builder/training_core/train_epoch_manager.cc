#include "train_epoch_manager.h"

namespace XER::ModelsBuilder::Training {

EpochManager::EpochManager(int total_epochs) : total_(total_epochs) {}

void EpochManager::Advance(float epoch_loss) {
  last_loss_ = epoch_loss;
  ++current_;
}

}  // namespace XER::ModelsBuilder::Training
