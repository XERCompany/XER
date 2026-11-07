#pragma once

#include <string>
#include <functional>
#include <memory>
#include "content/audio/audio_player.h"
#include "audio/effects_rack/custom_effect_flow.h"
#include "audio/effects_rack/custom_effect_struct.h"

namespace App {

// Audio position callback
using PositionCallback = std::function<void(double current_time, double duration)>;

// Playback state callback
using StateCallback = std::function<void(Content::Audio::PlaybackState state)>;

class AudioController {
public:
    AudioController();
    ~AudioController();

    // Initialization
    bool Initialize(int sample_rate = 44100, int channel_count = 2);
    bool IsInitialized() const { return initialized_; }

    // Load audio file
    bool LoadFromFile(const std::string& file_path, std::string* error_out = nullptr);
    bool IsLoaded() const { return audio_player_.IsLoaded(); }

    // Playback control
    bool Play();
    bool Pause();
    bool Stop();
    bool Seek(double time_seconds);
    bool Mute(bool mute);
    bool IsMuted() const { return muted_; }

    // Live position tracking
    double GetCurrentTime() const;
    double GetDuration() const;
    Content::Audio::PlaybackState GetState() const;
    void SetPositionCallback(PositionCallback callback);
    void SetStateCallback(StateCallback callback);

    // Volume control
    float GetVolume() const;
    void SetVolume(float volume);

    // Live effect processing
    bool ApplyEffectLive(const Engine::Audio::FX::CustomEffectPackage& package, std::string* error_out = nullptr);
    bool IsEffectLiveActive() const { return effect_flow_ != nullptr; }
    void DisableEffectLive();

    // Audio data access
    Content::Audio::AudioPlayer* GetPlayer() { return &audio_player_; }

private:
    void ProcessAudioWithEffects(std::vector<float>& samples);

    Content::Audio::AudioPlayer audio_player_;
    bool initialized_;
    bool muted_;
    float saved_volume_; // Volume before mute

    // Live effect processing
    std::unique_ptr<Engine::Audio::FX::CustomEffectFlow> effect_flow_;
    bool effect_enabled_;
};

}  // namespace App
