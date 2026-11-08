#include "csv_loader.h"

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace XER::MLData::Loading {

CSVLoader::CSVLoader() : delimiter_(',') {
}

std::shared_ptr<Types::Dataset> CSVLoader::LoadDataset(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("CSVLoader: cannot open file: " + path);
    }

    auto dataset = std::make_shared<Types::Dataset>("CSV_Dataset");
    std::string line;

    while (std::getline(file, line)) {
        if (line.empty()) continue;
        std::istringstream ss(line);
        std::string cell;
        std::vector<float> values;
        while (std::getline(ss, cell, delimiter_)) {
            try {
                values.push_back(std::stof(cell));
            } catch (...) {
                values.push_back(0.0f);
            }
        }
        if (values.empty()) continue;

        uint32_t n = static_cast<uint32_t>(values.size());
        auto tensor = std::make_shared<Types::Tensor>(
            std::vector<uint32_t>{1, n}, Types::DataType::FLOAT32);
        float* dst = static_cast<float*>(tensor->GetData());
        if (dst) {
            for (uint32_t i = 0; i < n; ++i) dst[i] = values[i];
        }
        auto batch = std::make_shared<Types::Batch>(1);
        batch->AddSample(tensor);
        dataset->AddBatch(batch);
    }

    return dataset;
}

bool CSVLoader::SaveDataset(const std::string& path, const Types::Dataset& dataset) {
    std::ofstream file(path);
    if (!file.is_open()) return false;

    for (size_t b = 0; b < dataset.GetBatchCount(); ++b) {
        auto batch = dataset.GetBatch(b);
        if (!batch) continue;
        for (size_t s = 0; s < batch->GetBatchSize(); ++s) {
            auto tensor = batch->GetSample(s);
            if (!tensor || !tensor->GetData()) continue;
            const float* src = static_cast<const float*>(tensor->GetData());
            const uint64_t n = tensor->GetElementCount();
            for (uint64_t i = 0; i < n; ++i) {
                if (i > 0) file << delimiter_;
                file << src[i];
            }
            file << '\n';
        }
    }
    return file.good();
}

} // namespace XER::MLData::Loading
