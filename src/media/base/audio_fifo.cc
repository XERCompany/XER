#include "media/base/audio_fifo.h"

#include <algorithm>
#include <cstring>

namespace XER::Media {

AudioFifo::AudioFifo(int channels, int max_frames)
    : bus_(AudioBus::Create(channels, max_frames)) {}

int AudioFifo::Push(const AudioBus& src, int frames) {
    const int room = bus_.frames() - frames_;
    const int n = std::min(frames, room);
    for (int c = 0; c < bus_.channels(); ++c) {
        float* dst = bus_.channel(c);
        const float* s = src.channel(c);
        int first = std::min(n, bus_.frames() - write_pos_);
        std::memcpy(dst + write_pos_, s, first * sizeof(float));
        std::memcpy(dst, s + first, (n - first) * sizeof(float));
    }
    write_pos_ = (write_pos_ + n) % bus_.frames();
    frames_ += n;
    return n;
}

int AudioFifo::Pop(AudioBus* dest, int frames) {
    const int n = std::min(frames, frames_);
    for (int c = 0; c < bus_.channels(); ++c) {
        float* d = dest->channel(c);
        const float* s = bus_.channel(c);
        int first = std::min(n, bus_.frames() - read_pos_);
        std::memcpy(d, s + read_pos_, first * sizeof(float));
        std::memcpy(d + first, s, (n - first) * sizeof(float));
        if (frames > n) std::fill(d + n, d + frames, 0.0f);
    }
    read_pos_ = (read_pos_ + n) % bus_.frames();
    frames_ -= n;
    return n;
}

void AudioFifo::Clear() { frames_ = read_pos_ = write_pos_ = 0; }

}  // namespace XER::Media
