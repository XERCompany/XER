#include "media/base/video_types.h"

namespace XER::Media {

int PixelFormatBytesPerPixel(PixelFormat format) {
    switch (format) {
        case PixelFormat::kBGRA:
        case PixelFormat::kRGBA: return 4;
        case PixelFormat::kYUY2: return 2;
        default: return 0;
    }
}

const char* PixelFormatName(PixelFormat format) {
    switch (format) {
        case PixelFormat::kBGRA: return "bgra";
        case PixelFormat::kRGBA: return "rgba";
        case PixelFormat::kI420: return "i420";
        case PixelFormat::kNV12: return "nv12";
        case PixelFormat::kYUY2: return "yuy2";
        default: return "unknown";
    }
}

bool PixelFormatIsPlanar(PixelFormat format) {
    return format == PixelFormat::kI420 || format == PixelFormat::kNV12;
}

}  // namespace XER::Media
