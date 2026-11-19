#include "media/base/channel_layout.h"

namespace XER::Media {

int ChannelLayoutCount(ChannelLayout layout) {
    switch (layout) {
        case ChannelLayout::kMono: return 1;
        case ChannelLayout::kStereo: return 2;
        case ChannelLayout::kSurround_5_1: return 6;
        case ChannelLayout::kSurround_7_1: return 8;
        default: return 0;
    }
}

const char* ChannelLayoutName(ChannelLayout layout) {
    switch (layout) {
        case ChannelLayout::kMono: return "mono";
        case ChannelLayout::kStereo: return "stereo";
        case ChannelLayout::kSurround_5_1: return "5.1";
        case ChannelLayout::kSurround_7_1: return "7.1";
        default: return "unknown";
    }
}

ChannelLayout ChannelLayoutFromCount(int channels) {
    switch (channels) {
        case 1: return ChannelLayout::kMono;
        case 2: return ChannelLayout::kStereo;
        case 6: return ChannelLayout::kSurround_5_1;
        case 8: return ChannelLayout::kSurround_7_1;
        default: return ChannelLayout::kUnknown;
    }
}

}  // namespace XER::Media
