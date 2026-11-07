#pragma once

#include <memory>
#include <string>
#include <functional>
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>

#include "audio/audio_core/audio_source_loader.h"

namespace Content::Audio {

// Audio playback state
enum class PlaybackState {
    kStopped,
    kPlaying,
    kPaused,
    kError
};

// Playback position callback
using PositionCallback = std::function<void(double current_time, double duration)>;

// State change callback
using StateCallback = std::function<void(PlaybackState state)>;

class AudioPlayer {
public:
    AudioPlayer();
    ~AudioPlayer();

    // Initialization
    bool Initialize(int sample_rate = 44100, int channel_count = 2);
    bool IsInitialized() const { return initialized_; }

    // Load audio file
    bool LoadFromFile(const std::string& file_path, std::string* error_out = nullptr);
    bool IsLoaded() const { return audio_buffer_.frame_count > 0; }

    // Playback control
    bool Play();
    bool Pause();
    bool Stop();
    bool Seek(double time_seconds);

    // State
    PlaybackState GetState() const { return state_; }
    double GetCurrentTime() const { return current_time_; }
    double GetDuration() const;
    float GetVolume() const { return volume_; }
    void SetVolume(float volume);

    // Callbacks
    void SetPositionCallback(PositionCallback callback) { position_callback_ = callback; }
    void SetStateCallback(StateCallback callback) { state_callback_ = callback; }

    // Public for thread function
    void ProcessAudio();

private:
    bool InitializeWASAPI();
    void CleanupWASAPI();
    void UpdateState(PlaybackState new_state);

    // WASAPI handles
    IMMDeviceEnumerator* device_enumerator_;
    IMMDevice* device_;
    IAudioClient* audio_client_;
    IAudioRenderClient* render_client_;

    // Audio configuration
    int sample_rate_;
    int channel_count_;
    UINT32 buffer_frame_count_;

    // Audio data
    Engine::Audio::Core::AudioSourceBuffer audio_buffer_;
    size_t playback_position_;

    // Playback state
    PlaybackState state_;
    double current_time_;
    float volume_;
    bool initialized_;

    // Threading
    HANDLE audio_thread_;
    bool stop_thread_;

    // Callbacks
    PositionCallback position_callback_;
    StateCallback state_callback_;
};

}  // namespace Content::Audio
