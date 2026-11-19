#include "media/base/audio_bus.h"

#include <algorithm>
#include <cstring>

namespace XER::Media {

AudioBus AudioBus::Create(int channels, int frames) {
    AudioBus bus;
    bus.channels_ = channels;
    bus.frames_ = frames;
    bus.data_.assign(static_cast<size_t>(channels) * frames, 0.0f);
    return bus;
}

AudioBus AudioBus::CreateForLayout(ChannelLayout layout, int frames) {
    return Create(ChannelLayoutCount(layout), frames);
}

void AudioBus::Zero() { std::fill(data_.begin(), data_.end(), 0.0f); }

void AudioBus::FromInterleaved(const float* src, int frames) {
    const int n = std::min(frames, frames_);
    for (int c = 0; c < channels_; ++c)
        for (int f = 0; f < n; ++f) channel(c)[f] = src[f * channels_ + c];
}

void AudioBus::ToInterleaved(int frames, float* dest) const {
    const int n = std::min(frames, frames_);
    for (int c = 0; c < channels_; ++c)
        for (int f = 0; f < n; ++f) dest[f * channels_ + c] = channel(c)[f];
}

void AudioBus::FromInterleavedFormat(SampleFormat fmt,
                                     const std::uint8_t* src, int frames) {
    const int n = std::min(frames, frames_);
    if (fmt == SampleFormat::kF32) {
        FromInterleaved(reinterpret_cast<const float*>(src), n);
        return;
    }
    if (fmt == SampleFormat::kS16) {
        const auto* s = reinterpret_cast<const std::int16_t*>(src);
        for (int c = 0; c < channels_; ++c)
            for (int f = 0; f < n; ++f)
                channel(c)[f] = static_cast<float>(s[f * channels_ + c]) /
                                32768.0f;
    }
}

void AudioBus::ToInterleavedFormat(SampleFormat fmt, int frames,
                                   std::uint8_t* dest) const {
    const int n = std::min(frames, frames_);
    if (fmt == SampleFormat::kF32) {
        ToInterleaved(n, reinterpret_cast<float*>(dest));
        return;
    }
    if (fmt == SampleFormat::kS16) {
        auto* s = reinterpret_cast<std::int16_t*>(dest);
        for (int c = 0; c < channels_; ++c)
            for (int f = 0; f < n; ++f) {
                float v = channel(c)[f];
                v = v < -1.f ? -1.f : (v > 1.f ? 1.f : v);
                s[f * channels_ + c] = static_cast<std::int16_t>(v * 32767.f);
            }
    }
}

}  // namespace XER::Media
