#include "media/base/audio_clock.h"

namespace XER::Media {

void AudioClock::WroteAudio(int frames) {
    if (frames > 0) frames_written_ += frames;
}

TimeTicks AudioClock::Now() const {
    if (paused_) return paused_at_;
    const std::int64_t f = frames_written_.load();
    return base_us_ + f * 1000000 / sample_rate_;
}

void AudioClock::Reset(int frames_written) {
    frames_written_ = frames_written;
    paused_at_ = 0;
    paused_ = false;
}

void AudioClock::Pause() {
    if (paused_) return;
    paused_at_ = Now();
    paused_ = true;
}

void AudioClock::Resume() {
    if (!paused_) return;
    base_us_ = paused_at_ - frames_written_.load() * 1000000 / sample_rate_;
    paused_ = false;
}

}  // namespace XER::Media
