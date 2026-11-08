#pragma once
#include <cstdint>
#include <vector>

namespace XER::Bridge::Skia {
    bool EncodeImageWEBP(const std::vector<uint32_t>& pixels, int width, int height,
                         int quality, std::vector<uint8_t>* out_bytes);
    bool EncodeImageGIF(const std::vector<uint32_t>& pixels, int width, int height,
                        std::vector<uint8_t>* out_bytes);
}
