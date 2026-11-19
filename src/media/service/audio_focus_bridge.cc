#include "media/service/audio_focus_bridge.h"

namespace XER::Media {

AudioFocusBridge::AudioFocusBridge(
    std::function<void(double)> apply_volume,
    std::function<void(bool)> apply_pause, double normal_volume,
    double duck_gain)
    : apply_volume_(std::move(apply_volume)),
      apply_pause_(std::move(apply_pause)),
      normal_volume_(normal_volume),
      duck_gain_(duck_gain) {}

MediaSessionImpl::StateCallback AudioFocusBridge::MakeSessionCallback() {
    return [this](SessionState s, bool duck) { OnStateChanged(s, duck); };
}

void AudioFocusBridge::AttachStream(AudioOutputStream* stream) {
    apply_volume_ = [stream](double v) { stream->SetVolume(v); };
    apply_pause_ = [stream](bool p) {
        if (p) stream->Stop();
    };
}

void AudioFocusBridge::OnStateChanged(SessionState state, bool ducking) {
    if (state == SessionState::kSuspended ||
        state == SessionState::kInactive) {
        if (apply_pause_) apply_pause_(true);
        return;
    }
    if (apply_pause_) apply_pause_(false);
    if (apply_volume_)
        apply_volume_(ducking ? normal_volume_ * duck_gain_
                              : normal_volume_);
}

}  // namespace XER::Media
