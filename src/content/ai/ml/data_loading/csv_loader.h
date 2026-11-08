#pragma once

#include "data_loader.h"

namespace XER::MLData::Loading {

class CSVLoader : public DataLoader {
public:
    CSVLoader();
    
    std::shared_ptr<Types::Dataset> LoadDataset(const std::string& path) override;
    bool SaveDataset(const std::string& path, const Types::Dataset& dataset) override;
    
    void SetDelimiter(char delim) { delimiter_ = delim; }
    char GetDelimiter() const { return delimiter_; }

private:
    char delimiter_;
};

} // namespace XER::MLData::Loading
