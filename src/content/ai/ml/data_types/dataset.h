#pragma once

#include "batch.h"
#include <string>
#include <vector>

namespace XER::MLData::Types {

class Dataset {
public:
    Dataset();
    explicit Dataset(const std::string& name);
    
    void AddBatch(std::shared_ptr<Batch> batch);
    std::shared_ptr<Batch> GetBatch(size_t index) const;
    
    size_t GetBatchCount() const { return batches_.size(); }
    std::string GetDatasetName() const { return name_; }
    void SetDatasetName(const std::string& name) { name_ = name; }
    
    uint64_t GetTotalSamples() const;

private:
    std::vector<std::shared_ptr<Batch>> batches_;
    std::string name_;
};

} // namespace XER::MLData::Types
