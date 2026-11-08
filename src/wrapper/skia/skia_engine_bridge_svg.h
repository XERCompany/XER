#pragma once
#include <cstdint>
#include <vector>
#include <string>

namespace XER::Bridge::Skia {
    // Renders an SVG string to the target pixel buffer
    bool RasterDrawSVG(std::vector<uint32_t>* pixels, int width, int height,
                       const std::string& svg_content, float x, float y, float scale);
}
