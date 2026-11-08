#pragma once

#include "../data_types/dataset.h"
#include <string>
#include <functional>

namespace XER::MLData::Loading {

using ProgressCallback = std::function<void(float)>;

class DataLoader {
public:
    DataLoader();
    virtual ~DataLoader() = default;
    
    virtual std::shared_ptr<Types::Dataset> LoadDataset(const std::string& path);
    virtual bool SaveDataset(const std::string& path, const Types::Dataset& dataset);
    
    void SetProgressCallback(ProgressCallback callback) { progress_callback_ = callback; }

protected:
    ProgressCallback progress_callback_;
};

} // namespace XER::MLData::Loading
