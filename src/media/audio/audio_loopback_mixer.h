/**
 * AudioLoopbackMixer — mixes multiple captured system-audio blocks into
 * one stream for loopback capture / "share tab audio" (design doc).
 */
#pragma once

#include "media/base/audio_bus.h"

namespace XER::Media {

class AudioLoopbackMixer {
public:
    // Mixes |src| into the internal accumulator (clamped f32 sum).
    void Mix(const AudioBus& src, int frames);
    // Copies the mix into |dst| and resets the accumulator.
    void Flush(AudioBus* dst, int frames);

private:
    AudioBus accum_ = AudioBus::Create(2, 8192);
};

}  // namespace XER::Media
