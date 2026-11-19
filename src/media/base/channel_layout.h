/**
 * Channel layouts (subset of the WebAudio layout set).
 */
#pragma once

#include <cstdint>

namespace XER::Media {

enum class ChannelLayout : std::uint8_t {
    kMono,
    kStereo,
    kSurround_5_1,
    kSurround_7_1,
    kUnknown,
};

int ChannelLayoutCount(ChannelLayout layout);
const char* ChannelLayoutName(ChannelLayout layout);
ChannelLayout ChannelLayoutFromCount(int channels);

}  // namespace XER::Media
