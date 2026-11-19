/**
 * AudioFocusBridge — connects the AudioFocusManager duck/suspend state
 * (src/media/session, MediaCore) to real output streams (AudioManager /
 * AudioOutputStream, MediaAudio). Per the design doc a transient focus
 * loss ducks (volume * duck_gain); suspension pauses playback.
 */
#pragma once

#include <functional>

#include "media/audio/audio_output_stream.h"
#include "media/session/media_session.h"

namespace XER::Media {

class AudioFocusBridge {
public:
    // |apply_volume| receives the effective gain to push into the sink
    // (e.g. AudioOutputDevice::SetVolume). |apply_pause| toggles pause.
    AudioFocusBridge(std::function<void(double)> apply_volume,
                     std::function<void(bool)> apply_pause,
                     double normal_volume = 1.0,
                     double duck_gain = 0.2);

    // Binds the callbacks to a session's StateCallback.
    MediaSessionImpl::StateCallback MakeSessionCallback();
    // Convenience: drives a real AudioOutputStream directly.
    void AttachStream(AudioOutputStream* stream);

    // Forwards a session state change (call from session observer).
    void OnStateChanged(SessionState state, bool ducking);

private:
    std::function<void(double)> apply_volume_;
    std::function<void(bool)> apply_pause_;
    double normal_volume_;
    double duck_gain_;
};

}  // namespace XER::Media
