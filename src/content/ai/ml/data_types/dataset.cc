#include "dataset.h"

namespace XER::MLData::Types {

Dataset::Dataset() : name_("DefaultDataset") {
}

Dataset::Dataset(const std::string& name) : name_(name) {
}

void Dataset::AddBatch(std::shared_ptr<Batch> batch) {
    if (batch) {
        batches_.push_back(batch);
    }
}

std::shared_ptr<Batch> Dataset::GetBatch(size_t index) const {
    if (index < batches_.size()) {
        return batches_[index];
    }
    return nullptr;
}

uint64_t Dataset::GetTotalSamples() const {
    uint64_t total = 0;
    for (const auto& batch : batches_) {
        total += batch->GetBatchSize();
    }
    return total;
}

} // namespace XER::MLData::Types
