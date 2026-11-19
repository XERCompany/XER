/**
 * MediaTrack — one elementary track declared by a demuxer: codec id,
 * language, extradata (codec private), and stream defaults.
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "media/base/media_types.h"
#include "media/base/sample_format.h"
#include "media/base/video_types.h"

namespace XER::Media {

struct MediaTrack {
    std::uint32_t id = 0;
    StreamType type = StreamType::kUnknown;
    CodecId codec = CodecId::kUnknown;
    std::string codec_name;
    std::string language;
    // Codec-private extradata (avcC, hvcC, esds, opus header...).
    std::vector<std::uint8_t> extradata;
    // Video.
    int width = 0;
    int height = 0;
    PixelFormat pixel_format = PixelFormat::kUnknown;
    // Audio.
    int sample_rate = 0;
    int channels = 0;
    SampleFormat sample_format = SampleFormat::kUnknown;
    TimeTicks duration = kNoTimestamp;
};

}  // namespace XER::Media
