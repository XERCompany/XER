#pragma once

#include <memory>
#include <string>
#include <functional>
#include <vector>
#include <atomic>
#include <mutex>
#include <thread>
#include <deque>
#include <cstdint>
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>

#include "audio/audio_core/audio_source_loader.h"

// windows.h (WinBase.h) defines `GetCurrentTime()` as a macro alias for
// GetTickCount() on Desktop — it would silently rename the member below to
// `GetTickCount`, making the API name TU-dependent. Undef so the member
// keeps a stable name in every translation unit.
#undef GetCurrentTime

namespace XER::Content::Audio {

class WasapiDeviceNotifier;

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
    // Installs an already-decoded buffer (e.g. prefetched asynchronously by
    // AudioSourceLoader) without running the decoder again.
    bool LoadFromBuffer(XER::Audio::Core::AudioSourceBuffer&& buffer);
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

    // Optional in-place processor applied to each interleaved audio block
    // (frames * channel_count samples) right before it is written to WASAPI.
    // Runs on the audio thread — must be real-time safe and thread-safe
    // against whatever state it touches.
    using BlockProcessor = std::function<void(std::vector<float>&)>;
    void SetBlockProcessor(BlockProcessor processor) { block_processor_ = std::move(processor); }

    // Linear fade in/out around track boundaries: gain ramps 0->1 over the
    // first fade_in_seconds and 1->0 over the last fade_out_seconds.
    // 0 disables a side. Each fade is clamped to half the track duration so
    // the two can't overlap into a silent middle on short files.
    // Call before Play() for deterministic behaviour.
    void SetFadeInOut(double fade_in_seconds, double fade_out_seconds) {
        fade_in_seconds_ = fade_in_seconds;
        fade_out_seconds_ = fade_out_seconds;
    }

    // Crossfade: queues a decoded buffer as a "next" track. When the
    // current buffer enters its last fade_out_seconds, the queue front is
    // mixed in — current fades out while it fades in over the same window
    // (linear). At the boundary the front is promoted and playback
    // continues seamlessly; the following queued buffer then becomes the
    // next crossfade source, so transitions chain without gaps.
    // SetTrackAdvancedCallback fires on the audio thread at each promote
    // with the tag passed to SetPendingBuffer (-1 when unused).
    // Safe to call from any thread; entries are consumed in order.
    void SetPendingBuffer(XER::Audio::Core::AudioSourceBuffer&& buffer,
                          int64_t track_tag = -1);
    // Number of decoded tracks currently queued for crossfade.
    size_t PendingCount() const { return pending_count_.load(); }
    using TrackAdvancedCallback = std::function<void(int64_t track_tag)>;
    void SetTrackAdvancedCallback(TrackAdvancedCallback callback) {
        track_advanced_callback_ = std::move(callback);
    }

    int GetSampleRate() const { return sample_rate_; }
    int GetChannelCount() const { return channel_count_; }

    // Public for thread function
    void ProcessAudio();

private:
    bool InitializeWASAPI();
    // Binds IAudioClient/IAudioRenderClient on the current device_ and
    // adopts the endpoint mix format (shared mode requires it).
    bool BindRenderEndpoint();
    void CleanupWASAPI();

    // Default-device hot-swap: an IMMNotificationClient signals a Win32
    // event; MonitorLoop performs the actual rebind off the RPC thread.
    void RegisterDeviceMonitor();
    void UnregisterDeviceMonitor();
    void MonitorLoop();
    void HandleDefaultDeviceChange();
    // Rewrites audio_buffer_ in place when the new endpoint's mix format
    // differs (rate and/or channel count).
    bool ReformatBufferForDevice(int old_rate, int old_channels);
    // Converts a fade duration in seconds to a frame count, clamped to
    // half the loaded track (0 when fading is off or nothing is loaded).
    uint64_t FadeFrames(double seconds) const;
    // Stops the feed thread and render client and drops queued pending
    // buffers WITHOUT touching state_/position — used by Load* so the
    // buffer swap never races the audio thread.
    void StopFeedThread();
    // Drops any queued pending buffers. Caller must ensure the audio thread
    // is stopped (used by Stop()/Load*).
    void ClearPendingBuffer();
    // Starts the render client and spawns the feed thread. Caller must
    // hold control_mutex_. Does NOT check IsLoaded/state — Play() does.
    bool StartRendering();
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
    XER::Audio::Core::AudioSourceBuffer audio_buffer_;
    size_t playback_position_;

    // Playback state
    std::atomic<PlaybackState> state_;
    double current_time_;
    float volume_;
    double fade_in_seconds_;
    double fade_out_seconds_;
    bool initialized_;

    // Threading
    HANDLE audio_thread_;
    bool stop_thread_;

    // Device-change monitoring (guarded by control_mutex_ for the rebind
    // path; the monitor thread itself only waits on device_event_).
    std::recursive_mutex control_mutex_;
    HANDLE device_event_ = nullptr;
    std::thread monitor_thread_;
    std::atomic<bool> monitor_stop_{false};
    std::unique_ptr<WasapiDeviceNotifier> notifier_;

    // Crossfade pending queue. pending_staging_ is written under
    // pending_mutex_ by control threads; the audio thread drains it into
    // pending_queue_ at the top of each render iteration when
    // pending_commit_ is set. pending_count_ mirrors the queue depth for
    // lock-free control-thread queries.
    struct PendingEntry {
        XER::Audio::Core::AudioSourceBuffer buffer;
        int64_t track_tag = -1;
    };
    std::deque<PendingEntry> pending_queue_;     // audio-thread owned
    std::deque<PendingEntry> pending_staging_;   // control-thread, mutexed
    std::mutex pending_mutex_;
    std::atomic<bool> pending_commit_{false};
    std::atomic<size_t> pending_count_{0};
    size_t pending_position_ = 0;   // interleaved cursor into queue front
    TrackAdvancedCallback track_advanced_callback_;

    // Callbacks
    PositionCallback position_callback_;
    StateCallback state_callback_;

    // Live DSP hook + scratch buffer (audio thread only).
    BlockProcessor block_processor_;
    std::vector<float> block_scratch_;
};

}  // namespace XER::Content::Audio
