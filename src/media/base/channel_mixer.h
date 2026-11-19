/**
 * ChannelMixer — channel layout conversion (design doc: stereo <-> mono
 * and 5.1/7.1 downmix happen between the decoder's native layout and the
 * device layout chosen by AudioManager).
 */
#pragma once

#include "media/base/audio_bus.h"
#include "media/base/channel_layout.h"

namespace XER::Media {

class ChannelMixer {
public:
    ChannelMixer(ChannelLayout from, ChannelLayout to) : from_(from), to_(to) {}

    bool CanConvert() const;
    // Converts |src| (layout from_) into |dst| (layout to_), f32 planar.
    void Transform(const AudioBus& src, AudioBus* dst) const;

private:
    ChannelLayout from_;
    ChannelLayout to_;
};

}  // namespace XER::Media
