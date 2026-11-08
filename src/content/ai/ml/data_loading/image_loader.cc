#include "image_loader.h"

#include <filesystem>
#include <stdexcept>
#include <cstring>

namespace XER::MLData::Loading {

ImageLoader::ImageLoader() : image_width_(224), image_height_(224) {
}

std::shared_ptr<Types::Dataset> ImageLoader::LoadDataset(const std::string& path) {
    namespace fs = std::filesystem;
    auto dataset = std::make_shared<Types::Dataset>("ImageDataset");

    const fs::path p(path);
    std::error_code ec;

    // If path is a directory, enumerate all image files.
    if (fs::is_directory(p, ec)) {
        for (const auto& entry : fs::directory_iterator(p, ec)) {
            if (!entry.is_regular_file()) continue;
            const auto ext = entry.path().extension().string();
            if (ext != ".jpg" && ext != ".jpeg" && ext != ".png" &&
                ext != ".bmp" && ext != ".tiff" && ext != ".tif") continue;

            // Emit a placeholder tensor per image (shape: 1 × channels × h × w).
            // Real pixel loading would use the image bridge.
            const uint32_t channels = 3;
            uint32_t w = image_width_, h = image_height_;
            auto tensor = std::make_shared<Types::Tensor>(
                std::vector<uint32_t>{1, channels, h, w}, Types::DataType::FLOAT32);
            tensor->Zero();

            auto batch = std::make_shared<Types::Batch>(1);
            batch->AddSample(tensor);
            dataset->AddBatch(batch);
        }
    } else {
        // Single file path.
        const uint32_t channels = 3;
        uint32_t w = image_width_, h = image_height_;
        auto tensor = std::make_shared<Types::Tensor>(
            std::vector<uint32_t>{1, channels, h, w}, Types::DataType::FLOAT32);
        tensor->Zero();

        auto batch = std::make_shared<Types::Batch>(1);
        batch->AddSample(tensor);
        dataset->AddBatch(batch);
    }

    return dataset;
}

void ImageLoader::SetImageSize(uint32_t width, uint32_t height) {
    image_width_ = width;
    image_height_ = height;
}

} // namespace XER::MLData::Loading
