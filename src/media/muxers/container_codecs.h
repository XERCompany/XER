/**
 * Container codec tables (design doc):
 *   webm: VP8, VP9, AV1, H.264 + Opus
 *   mkv : VP8, VP9, AV1, H.264, H.265 + Opus
 *   mp4 : VP9, AV1, H.264, H.265 + Opus, AAC
 * (webm is a subset of mkv that technically only supports vp8/vp9/av1.)
 */
#pragma once

#include <cstdint>
#include <vector>

#include "media/base/media_types.h"

namespace XER::Media {

enum class Container : uint8_t { kWebm, kMkv, kMp4 };

// Returns true if |codec| is allowed in |container| per the doc's table.
bool ContainerSupportsCodec(Container container, CodecId codec);
std::vector<CodecId> ContainerCodecs(Container container, StreamType type);
const char* ContainerName(Container container);
const char* ContainerExtension(Container container);  // ".webm" etc.

}  // namespace XER::Media
