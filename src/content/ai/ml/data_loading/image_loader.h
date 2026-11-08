#pragma once

#include "data_loader.h"

namespace XER::MLData::Loading {

class ImageLoader : public DataLoader {
public:
    ImageLoader();
    
    std::shared_ptr<Types::Dataset> LoadDataset(const std::string& path) override;
    void SetImageSize(uint32_t width, uint32_t height);

private:
    uint32_t image_width_;
    uint32_t image_height_;
};

} // namespace XER::MLData::Loading
