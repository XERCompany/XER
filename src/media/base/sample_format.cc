#include "media/base/sample_format.h"

#include <cstring>

namespace XER::Media {

int SampleFormatBytes(SampleFormat format) {
    switch (format) {
        case SampleFormat::kU8: return 1;
        case SampleFormat::kS16:
        case SampleFormat::kPlanarS16: return 2;
        case SampleFormat::kS32:
        case SampleFormat::kF32:
        case SampleFormat::kPlanarF32: return 4;
        default: return 0;
    }
}

const char* SampleFormatName(SampleFormat format) {
    switch (format) {
        case SampleFormat::kU8: return "u8";
        case SampleFormat::kS16: return "s16";
        case SampleFormat::kS32: return "s32";
        case SampleFormat::kF32: return "f32";
        case SampleFormat::kPlanarS16: return "s16p";
        case SampleFormat::kPlanarF32: return "f32p";
        default: return "unknown";
    }
}

SampleFormat SampleFormatFromName(const char* name) {
    if (!name) return SampleFormat::kUnknown;
    if (!std::strcmp(name, "u8")) return SampleFormat::kU8;
    if (!std::strcmp(name, "s16")) return SampleFormat::kS16;
    if (!std::strcmp(name, "s32")) return SampleFormat::kS32;
    if (!std::strcmp(name, "f32") || !std::strcmp(name, "flt"))
        return SampleFormat::kF32;
    if (!std::strcmp(name, "s16p")) return SampleFormat::kPlanarS16;
    if (!std::strcmp(name, "f32p") || !std::strcmp(name, "fltp"))
        return SampleFormat::kPlanarF32;
    return SampleFormat::kUnknown;
}

bool SampleFormatIsPlanar(SampleFormat format) {
    return format == SampleFormat::kPlanarS16 ||
           format == SampleFormat::kPlanarF32;
}

}  // namespace XER::Media
