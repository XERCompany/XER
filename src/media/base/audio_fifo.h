/**
 * AudioFifo — buffered f32 frame FIFO decoupling producer/consumer block
 * sizes (design doc: the renderer pushes decoder-sized blocks while the
 * sink pulls device-sized blocks).
 */
#pragma once

#include <vector>

#include "media/base/audio_bus.h"

namespace XER::Media {

class AudioFifo {
public:
    AudioFifo(int channels, int max_frames);

    int channels() const { return bus_.channels(); }
    int frames() const { return frames_; }        // buffered
    int max_frames() const { return bus_.frames(); }
    bool empty() const { return frames_ == 0; }

    // Appends |frames| planar samples from |src| (must match channels).
    // Returns frames actually written (<= frames when nearly full).
    int Push(const AudioBus& src, int frames);
    // Pops up to |frames| into |dest| (zero-fills the remainder).
    // Returns frames actually popped.
    int Pop(AudioBus* dest, int frames);
    void Clear();

private:
    AudioBus bus_ = AudioBus::Create(1, 1);
    int frames_ = 0;
    int read_pos_ = 0;
    int write_pos_ = 0;
};

}  // namespace XER::Media
