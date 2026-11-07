#include "app/app_audio_controller.h"

namespace App {

AudioController::AudioController()
    : initialized_(false)
    , muted_(false)
    , saved_volume_(1.0f)
    , effect_enabled_(false) {
}

AudioController::~AudioController() {
    if (initialized_) {
        audio_player_.Stop();
    }
    effect_flow_.reset();
}

bool AudioController::Initialize(int sample_rate, int channel_count) {
    if (!audio_player_.Initialize(sample_rate, channel_count)) {
        return false;
    }
    initialized_ = true;
    return true;
}

bool AudioController::LoadFromFile(const std::string& file_path, std::string* error_out) {
    if (!initialized_) {
        if (error_out) *error_out = "AudioController not initialized";
        return false;
    }
    return audio_player_.LoadFromFile(file_path, error_out);
}

bool AudioController::Play() {
    if (!initialized_) return false;
    return audio_player_.Play();
}

bool AudioController::Pause() {
    if (!initialized_) return false;
    return audio_player_.Pause();
}

bool AudioController::Stop() {
    if (!initialized_) return false;
    return audio_player_.Stop();
}

bool AudioController::Seek(double time_seconds) {
    if (!initialized_) return false;
    return audio_player_.Seek(time_seconds);
}

bool AudioController::Mute(bool mute) {
    if (!initialized_) return false;
    
    if (mute && !muted_) {
        saved_volume_ = audio_player_.GetVolume();
        audio_player_.SetVolume(0.0f);
        muted_ = true;
    } else if (!mute && muted_) {
        audio_player_.SetVolume(saved_volume_);
        muted_ = false;
    }
    
    return true;
}

double AudioController::GetCurrentTime() const {
    if (!initialized_) return 0.0;
    return audio_player_.GetCurrentTime();
}

double AudioController::GetDuration() const {
    if (!initialized_) return 0.0;
    return audio_player_.GetDuration();
}

Content::Audio::PlaybackState AudioController::GetState() const {
    if (!initialized_) return Content::Audio::PlaybackState::kStopped;
    return audio_player_.GetState();
}

void AudioController::SetPositionCallback(PositionCallback callback) {
    audio_player_.SetPositionCallback(callback);
}

void AudioController::SetStateCallback(StateCallback callback) {
    audio_player_.SetStateCallback(callback);
}

float AudioController::GetVolume() const {
    if (!initialized_) return 0.0f;
    return audio_player_.GetVolume();
}

void AudioController::SetVolume(float volume) {
    if (!initialized_) return;
        audio_player_.SetVolume(volume);
    if (!muted_) {
        saved_volume_ = volume;
    }
}

bool AudioController::ApplyEffectLive(const Engine::Audio::FX::CustomEffectPackage& package, std::string* error_out) {
    if (!initialized_) {
        if (error_out) *error_out = "AudioController not initialized";
        return false;
    }

    effect_flow_ = std::make_unique<Engine::Audio::FX::CustomEffectFlow>();
    int sample_rate = audio_player_.GetDuration() > 0 ? 44100 : 44100; // Use default for now
    if (!effect_flow_->Initialize(package, static_cast<float>(sample_rate), 1024, error_out)) {
        effect_flow_.reset();
        return false;
    }

    effect_enabled_ = true;
    return true;
}

void AudioController::DisableEffectLive() {
    effect_flow_.reset();
    effect_enabled_ = false;
}

void AudioController::ProcessAudioWithEffects(std::vector<float>& samples) {
    if (!effect_enabled_ || !effect_flow_) {
        return;
    }

    std::vector<float> output;
    std::string error;
    if (!effect_flow_->Process(samples, &output, &error)) {
        // If effect processing fails, just pass through
        return;
    }

    samples = std::move(output);
}

}  // namespace App
