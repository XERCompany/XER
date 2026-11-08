#pragma once

#include "tensor.h"
#include <vector>
#include <memory>

namespace XER::MLData::Types {

class Batch {
public:
    Batch();
    explicit Batch(size_t batch_size);
    
    void AddSample(std::shared_ptr<Tensor> sample);
    std::shared_ptr<Tensor> GetSample(size_t index) const;
    
    size_t GetBatchSize() const { return samples_.size(); }
    bool IsFull() const { return samples_.size() >= expected_size_; }
    
    const std::vector<std::shared_ptr<Tensor>>& GetSamples() const { return samples_; }

private:
    std::vector<std::shared_ptr<Tensor>> samples_;
    size_t expected_size_;
};

} // namespace XER::MLData::Types
