/**
 * Video pixel formats and color spaces for decoded frames.
 */
#pragma once

#include <cstdint>

namespace XER::Media {

enum class PixelFormat : std::uint8_t {
    kBGRA,
    kRGBA,
    kI420,
    kNV12,
    kYUY2,
    kUnknown,
};

enum class ColorSpace : std::uint8_t {
    kBT709,
    kBT601,
    kBT2020,
    kSRGB,
    kUnknown,
};

int PixelFormatBytesPerPixel(PixelFormat format);  // packed formats; 0 planar
const char* PixelFormatName(PixelFormat format);
bool PixelFormatIsPlanar(PixelFormat format);

}  // namespace XER::Media
