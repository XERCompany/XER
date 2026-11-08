#include "data_loader.h"

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace XER::MLData::Loading {

DataLoader::DataLoader() {
}

std::shared_ptr<Types::Dataset> DataLoader::LoadDataset(const std::string& path) {
    // Base implementation: load as raw binary floats (row of float32 values).
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("DataLoader: cannot open file: " + path);
    }

    auto dataset = std::make_shared<Types::Dataset>("LoadedDataset");

    // Determine file size to read all float32 values.
    file.seekg(0, std::ios::end);
    const auto size = static_cast<size_t>(file.tellg());
    file.seekg(0, std::ios::beg);

    const size_t n = size / sizeof(float);
    if (n == 0) return dataset;

    std::vector<float> buf(n);
    file.read(reinterpret_cast<char*>(buf.data()),
              static_cast<std::streamsize>(n * sizeof(float)));

    auto tensor = std::make_shared<Types::Tensor>(
        std::vector<uint32_t>{1, static_cast<uint32_t>(n)}, Types::DataType::FLOAT32);
    float* dst = static_cast<float*>(tensor->GetData());
    if (dst) {
        for (size_t i = 0; i < n; ++i) dst[i] = buf[i];
    }
    auto batch = std::make_shared<Types::Batch>(1);
    batch->AddSample(tensor);
    dataset->AddBatch(batch);

    return dataset;
}

bool DataLoader::SaveDataset(const std::string& path, const Types::Dataset& dataset) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) return false;

    for (size_t b = 0; b < dataset.GetBatchCount(); ++b) {
        auto batch = dataset.GetBatch(b);
        if (!batch) continue;
        for (size_t s = 0; s < batch->GetBatchSize(); ++s) {
            auto tensor = batch->GetSample(s);
            if (!tensor || !tensor->GetData()) continue;
            file.write(static_cast<const char*>(tensor->GetData()),
                       static_cast<std::streamsize>(tensor->GetMemorySize()));
        }
    }
    return file.good();
}

} // namespace XER::MLData::Loading
