#include <cstdint>
#pragma once

namespace XER::Bridge::Skia {
    // Create a Drop Shadow ImageFilter handle
    int ImageFilterCreateDropShadow(float dx, float dy, float sigma_x, float sigma_y, uint32_t color);
    // Create a Blur ImageFilter handle
    int ImageFilterCreateBlur(float sigma_x, float sigma_y);
    // Release image filter
    void ImageFilterRelease(int filter_id);
}
