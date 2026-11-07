#pragma once

#include <string>
#include <functional>
#include <memory>
#include <mutex>
#include "content/audio/audio_player.h"
#include "audio/effects_rack/custom_effect_flow.h"
#include "audio/effects_rack/custom_effect_struct.h"

namespace XER::App {

// Audio position callback
using PositionCallback = std::function<void(double current_time, double duration)>;

// Playback state callback
using StateCallback = std::function<void(XER::Content::Audio::PlaybackState state)>;

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
    XER::Content::Audio::PlaybackState GetState() const;
    void SetPositionCallback(PositionCallback callback);
    void SetStateCallback(StateCallback callback);

    // Volume control
    float GetVolume() const;
    void SetVolume(float volume);

    // Live effect processing
    bool ApplyEffectLive(const XER::Engine::Audio::FX::CustomEffectPackage& package, std::string* error_out = nullptr);
    bool IsEffectLiveActive() const {
        std::lock_guard<std::mutex> lock(effect_mutex_);
        return effect_flow_ != nullptr;
    }
    void DisableEffectLive();

    // Audio data access
    XER::Content::Audio::AudioPlayer* GetPlayer() { return &audio_player_; }

    // Optional per-channel block processor (e.g. a loaded VST3 plugin),
    // invoked on the audio thread for each mono channel after the FX chain.
    // Signature: (input, output, frames) -> success. Pass nullptr to clear.
    using BlockProcessor = std::function<bool(const float*, float*, uint32_t)>;
    void SetBlockProcessor(BlockProcessor processor);

private:
    void ProcessAudioWithEffects(std::vector<float>& samples);

    XER::Content::Audio::AudioPlayer audio_player_;
    bool initialized_;
    bool muted_;
    float saved_volume_; // Volume before mute

    // Live effect processing
    std::unique_ptr<XER::Engine::Audio::FX::CustomEffectFlow> effect_flow_;
    bool effect_enabled_;
    // Optional plugin block processor (VST3 etc.), same audio-thread path.
    BlockProcessor block_processor_;
    // Guards effect_flow_ between the UI thread (apply/disable) and the
    // audio thread (per-block Process). block_processor_ is set once at
    // load/unload time and read on the audio thread.
    mutable std::mutex effect_mutex_;
};

}  // namespace XER::App