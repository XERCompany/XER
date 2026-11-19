#include "media/base/channel_mixer.h"

#include <algorithm>

namespace XER::Media {

bool ChannelMixer::CanConvert() const {
    return from_ != ChannelLayout::kUnknown &&
           to_ != ChannelLayout::kUnknown;
}

void ChannelMixer::Transform(const AudioBus& src, AudioBus* dst) const {
    if (!CanConvert()) return;
    const int n = std::min(src.frames(), dst->frames());
    const int sc = src.channels();
    const int dc = dst->channels();
    if (sc == dc) {
        for (int c = 0; c < dc; ++c)
            std::copy_n(src.channel(c), n, dst->channel(c));
        return;
    }
    if (sc == 1) {  // mono -> N: duplicate
        for (int c = 0; c < dc; ++c)
            std::copy_n(src.channel(0), n, dst->channel(c));
        return;
    }
    if (dc == 1) {  // N -> mono: average
        const float g = 1.0f / static_cast<float>(sc);
        float* m = dst->channel(0);
        std::fill_n(m, n, 0.0f);
        for (int c = 0; c < sc; ++c)
            for (int f = 0; f < n; ++f) m[f] += src.channel(c)[f] * g;
        return;
    }
    // Stereo <-> multichannel: L,R passthrough; extra channels get the
    // L/R average; surplus source channels are dropped.
    for (int f = 0; f < n; ++f) {
        const float l = src.channel(0)[f];
        const float r = sc > 1 ? src.channel(1)[f] : l;
        for (int c = 0; c < dc; ++c)
            dst->channel(c)[f] = c < 2 ? (c == 0 ? l : r) : (l + r) * 0.5f;
    }
}

}  // namespace XER::Media
