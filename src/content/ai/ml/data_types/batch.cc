#include "batch.h"

namespace XER::MLData::Types {

Batch::Batch() : expected_size_(32) {
}

Batch::Batch(size_t batch_size) : expected_size_(batch_size) {
}

void Batch::AddSample(std::shared_ptr<Tensor> sample) {
    if (sample && samples_.size() < expected_size_) {
        samples_.push_back(sample);
    }
}

std::shared_ptr<Tensor> Batch::GetSample(size_t index) const {
    if (index < samples_.size()) {
        return samples_[index];
    }
    return nullptr;
}

} // namespace XER::MLData::Types
