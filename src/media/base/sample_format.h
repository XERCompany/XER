/**
 * Audio sample formats understood by the media pipeline.
 */
#pragma once

#include <cstdint>

namespace XER::Media {

enum class SampleFormat : std::uint8_t {
    kU8,
    kS16,
    kS32,
    kF32,
    kPlanarS16,
    kPlanarF32,
    kUnknown,
};

int SampleFormatBytes(SampleFormat format);
const char* SampleFormatName(SampleFormat format);
SampleFormat SampleFormatFromName(const char* name);
bool SampleFormatIsPlanar(SampleFormat format);

}  // namespace XER::Media
