#include "media/audio/audio_loopback_mixer.h"

#include <algorithm>

namespace XER::Media {

void AudioLoopbackMixer::Mix(const AudioBus& src, int frames) {
    const int n = std::min(frames, accum_.frames());
    for (int c = 0; c < accum_.channels() && c < src.channels(); ++c) {
        float* d = accum_.channel(c);
        const float* s = src.channel(c);
        for (int f = 0; f < n; ++f) {
            const float v = d[f] + s[f];
            d[f] = v < -1.f ? -1.f : (v > 1.f ? 1.f : v);
        }
    }
}

void AudioLoopbackMixer::Flush(AudioBus* dst, int frames) {
    const int n = std::min(frames, accum_.frames());
    for (int c = 0; c < dst->channels() && c < accum_.channels(); ++c)
        std::copy_n(accum_.channel(c), n, dst->channel(c));
    accum_.Zero();
}

}  // namespace XER::Media
