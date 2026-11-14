#include "content/audio/audio_player.h"

#include <algorithm>
#include <cmath>
#include <cfloat>
#include <cstring>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <propkey.h>
#include <thread>

#include "audio/audio_core/audio_sample_rate_converter.h"

namespace XER::Content::Audio {

// IMMNotificationClient that signals a Win32 event when the default render
// endpoint (console role — the one GetDefaultAudioEndpoint binds) changes.
// Callbacks arrive on COM RPC threads: they only SetEvent and never touch
// player state, so they stay real-time safe. Defined at namespace scope to
// match the forward declaration in audio_player.h (unique_ptr member).
class WasapiDeviceNotifier final : public IMMNotificationClient {
public:
    explicit WasapiDeviceNotifier(HANDLE event) : event_(event) {}

    // IUnknown — the enumerator AddRefs/Release()s the callback; we keep a
    // single static lifetime owned by AudioPlayer, so refcounting is a no-op.
    ULONG STDMETHODCALLTYPE AddRef() override { return 2; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override {
        if (object == nullptr) {
            return E_POINTER;
        }
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IMMNotificationClient)) {
            *object = static_cast<IMMNotificationClient*>(this);
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }

    // IMMNotificationClient
    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR, DWORD) override {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow, ERole role,
                                                     LPCWSTR) override {
        if (flow == eRender && role == eConsole) {
            SetEvent(event_);
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR,
                                                     const PROPERTYKEY) override {
        return S_OK;
    }

private:
    HANDLE event_;
};

static DWORD WINAPI AudioThreadProc(LPVOID param) {
    AudioPlayer* player = static_cast<AudioPlayer*>(param);
    player->ProcessAudio();
    return 0;
}

AudioPlayer::AudioPlayer()
    : device_enumerator_(nullptr)
    , device_(nullptr)
    , audio_client_(nullptr)
    , render_client_(nullptr)
    , sample_rate_(44100)
    , channel_count_(2)
    , buffer_frame_count_(0)
    , playback_position_(0)
    , state_(PlaybackState::kStopped)
    , current_time_(0.0)
    , volume_(1.0f)
    , fade_in_seconds_(0.0)
    , fade_out_seconds_(0.0)
    , initialized_(false)
    , audio_thread_(nullptr)
    , stop_thread_(false) {
}

AudioPlayer::~AudioPlayer() {
    Stop();
    UnregisterDeviceMonitor();
    CleanupWASAPI();
}

bool AudioPlayer::Initialize(int sample_rate, int channel_count) {
    sample_rate_ = sample_rate;
    channel_count_ = channel_count;

    if (!InitializeWASAPI()) {
        return false;
    }

    initialized_ = true;
    return true;
}

bool AudioPlayer::InitializeWASAPI() {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        return false;
    }

    hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator),
        nullptr,
        CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator),
        reinterpret_cast<void**>(&device_enumerator_)
    );

    if (FAILED(hr)) {
        CoUninitialize();
        return false;
    }

    hr = device_enumerator_->GetDefaultAudioEndpoint(eRender, eConsole, &device_);
    if (FAILED(hr)) {
        device_enumerator_->Release();
        device_enumerator_ = nullptr;
        CoUninitialize();
        return false;
    }

    if (!BindRenderEndpoint()) {
        device_->Release();
        device_ = nullptr;
        device_enumerator_->Release();
        device_enumerator_ = nullptr;
        CoUninitialize();
        return false;
    }

    RegisterDeviceMonitor();
    return true;
}

// Activates IAudioClient on device_ and IAudioRenderClient on the client,
// adopting the endpoint mix format. On failure only the objects created
// here are released — device_/enumerator stay owned by the caller.
bool AudioPlayer::BindRenderEndpoint() {
    HRESULT hr = device_->Activate(
        __uuidof(IAudioClient),
        CLSCTX_ALL,
        nullptr,
        reinterpret_cast<void**>(&audio_client_)
    );

    if (FAILED(hr)) {
        return false;
    }

    WAVEFORMATEX* mix_format = nullptr;
    hr = audio_client_->GetMixFormat(&mix_format);
    if (FAILED(hr)) {
        audio_client_->Release();
        audio_client_ = nullptr;
        return false;
    }

    // Use device's native format for simplicity
    REFERENCE_TIME buffer_duration = 10000000; // 1 second
    hr = audio_client_->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        0,
        buffer_duration,
        0,
        mix_format,
        nullptr
    );

    if (FAILED(hr)) {
        CoTaskMemFree(mix_format);
        audio_client_->Release();
        audio_client_ = nullptr;
        return false;
    }

    // Update sample rate and channel count to match device
    sample_rate_ = mix_format->nSamplesPerSec;
    channel_count_ = mix_format->nChannels;

    CoTaskMemFree(mix_format);

    hr = audio_client_->GetBufferSize(&buffer_frame_count_);
    if (FAILED(hr)) {
        audio_client_->Release();
        audio_client_ = nullptr;
        return false;
    }

    hr = audio_client_->GetService(
        __uuidof(IAudioRenderClient),
        reinterpret_cast<void**>(&render_client_)
    );

    if (FAILED(hr)) {
        audio_client_->Release();
        audio_client_ = nullptr;
        render_client_ = nullptr;
        return false;
    }

    return true;
}

void AudioPlayer::RegisterDeviceMonitor() {
    if (device_enumerator_ == nullptr || device_event_ != nullptr) {
        return;
    }
    device_event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (device_event_ == nullptr) {
        return;
    }
    notifier_ = std::make_unique<WasapiDeviceNotifier>(device_event_);
    if (FAILED(device_enumerator_->RegisterEndpointNotificationCallback(
            notifier_.get()))) {
        notifier_.reset();
        CloseHandle(device_event_);
        device_event_ = nullptr;
        return;
    }
    monitor_stop_.store(false);
    monitor_thread_ = std::thread(&AudioPlayer::MonitorLoop, this);
}

void AudioPlayer::UnregisterDeviceMonitor() {
    monitor_stop_.store(true);
    if (device_event_ != nullptr) {
        SetEvent(device_event_);
    }
    if (monitor_thread_.joinable()) {
        monitor_thread_.join();
    }
    if (device_enumerator_ != nullptr && notifier_ != nullptr) {
        device_enumerator_->UnregisterEndpointNotificationCallback(
            notifier_.get());
    }
    notifier_.reset();
    if (device_event_ != nullptr) {
        CloseHandle(device_event_);
        device_event_ = nullptr;
    }
}

void AudioPlayer::MonitorLoop() {
    while (WaitForSingleObject(device_event_, INFINITE) == WAIT_OBJECT_0) {
        if (monitor_stop_.load()) {
            break;
        }
        HandleDefaultDeviceChange();
    }
}

// Rebinds the render endpoint after the default output device changed
// (e.g. speakers -> headphones). Preserves playback position and resumes
// when the track was playing.
void AudioPlayer::HandleDefaultDeviceChange() {
    std::lock_guard<std::recursive_mutex> lock(control_mutex_);
    if (!initialized_ || device_enumerator_ == nullptr) {
        return;
    }

    const bool was_playing = state_.load() == PlaybackState::kPlaying;
    const bool had_audio = IsLoaded();
    const double resume_time = current_time_;
    const int old_rate = sample_rate_;
    const int old_channels = channel_count_;

    // Stop the feed thread first — it dereferences audio_client_/
    // render_client_ every iteration. state_ intentionally stays
    // kPlaying/kPaused: dropping to kStopped would look like a natural
    // track end to pollers and trigger a track advance mid-rebind.
    // StopFeedThread also drops queued crossfade buffers (decoded for the
    // old endpoint's format).
    StopFeedThread();

    if (render_client_) {
        render_client_->Release();
        render_client_ = nullptr;
    }
    if (audio_client_) {
        audio_client_->Release();
        audio_client_ = nullptr;
    }
    if (device_) {
        device_->Release();
        device_ = nullptr;
    }

    IMMDevice* new_device = nullptr;
    const HRESULT hr = device_enumerator_->GetDefaultAudioEndpoint(
        eRender, eConsole, &new_device);
    if (FAILED(hr) || new_device == nullptr) {
        UpdateState(PlaybackState::kError);
        return;
    }
    device_ = new_device;

    if (!BindRenderEndpoint()) {
        device_->Release();
        device_ = nullptr;
        UpdateState(PlaybackState::kError);
        return;
    }

    // Shared-mode mix format may differ between endpoints — rewrite the
    // decoded buffer to the new rate/channel layout before resuming.
    // A failed reformat leaves samples in the old layout: better to surface
    // an error than play noise.
    if ((sample_rate_ != old_rate || channel_count_ != old_channels) &&
        !ReformatBufferForDevice(old_rate, old_channels)) {
        UpdateState(PlaybackState::kError);
        return;
    }

    // Restore position: Seek recomputes playback_position_ against the
    // (possibly reformatted) buffer, so it works for both format paths.
    if (had_audio) {
        Seek(resume_time);
    }

    // state_ never left kPlaying, so StartRendering() restarts the feed
    // thread directly (Play() would early-return on the same state).
    // Paused/stopped sessions keep their state — nothing to restart.
    if (was_playing) {
        StartRendering();
    }
}

// Rewrites audio_buffer_ in place when the new endpoint's mix format
// differs: deinterleave -> channel remix -> per-channel resample ->
// reinterleave. Lossy only across the format boundary (rare: most Windows
// endpoints share the same 48 kHz/stereo float mix format).
bool AudioPlayer::ReformatBufferForDevice(int old_rate, int old_channels) {
    if (!IsLoaded()) {
        return true;
    }
    const int new_rate = sample_rate_;
    const int new_channels = channel_count_;
    const size_t frames = audio_buffer_.frame_count;
    if (frames == 0) {
        return true;
    }

    // Deinterleave to planar.
    std::vector<std::vector<float>> planar(
        static_cast<size_t>(old_channels), std::vector<float>(frames, 0.0f));
    const std::vector<float>& src = audio_buffer_.samples;
    for (size_t f = 0; f < frames; ++f) {
        for (int ch = 0; ch < old_channels; ++ch) {
            planar[static_cast<size_t>(ch)][f] =
                src[f * static_cast<size_t>(old_channels) +
                    static_cast<size_t>(ch)];
        }
    }

    // Channel remix to the new layout.
    std::vector<std::vector<float>> adjusted(
        static_cast<size_t>(new_channels));
    for (auto& ch : adjusted) {
        ch.assign(frames, 0.0f);
    }
    if (new_channels == old_channels) {
        adjusted = planar;
    } else if (new_channels == 1) {
        for (size_t f = 0; f < frames; ++f) {
            float sum = 0.0f;
            for (int ch = 0; ch < old_channels; ++ch) {
                sum += planar[static_cast<size_t>(ch)][f];
            }
            adjusted[0][f] = sum / static_cast<float>(old_channels);
        }
    } else if (old_channels == 1) {
        for (int ch = 0; ch < new_channels; ++ch) {
            adjusted[static_cast<size_t>(ch)] = planar[0];
        }
    } else {
        for (int ch = 0; ch < new_channels; ++ch) {
            const int src_ch = (std::min)(ch, old_channels - 1);
            adjusted[static_cast<size_t>(ch)] =
                planar[static_cast<size_t>(src_ch)];
        }
    }

    // Per-channel sample-rate conversion (one-shot over the full buffer).
    // A fresh converter per channel: it carries streaming phase state
    // (buffer_pos_) that must not leak across channels.
    const bool needs_resample = new_rate != old_rate;
    std::vector<std::vector<float>> out(
        static_cast<size_t>(new_channels));
    size_t out_frames = frames;
    if (needs_resample) {
        out_frames = static_cast<size_t>(
            std::floor((static_cast<double>(frames) - 1.0) *
                       static_cast<double>(new_rate) / old_rate)) + 2;
        for (int ch = 0; ch < new_channels; ++ch) {
            XER::Audio::Core::AudioSampleRateConverter converter(
                XER::Audio::Core::ResampleQuality::HIGH);
            if (!converter.Initialize(old_rate, new_rate)) {
                return false;
            }
            out[static_cast<size_t>(ch)].assign(out_frames, 0.0f);
            size_t produced = 0;
            if (!converter.Convert(adjusted[static_cast<size_t>(ch)].data(),
                                   frames,
                                   out[static_cast<size_t>(ch)].data(),
                                   produced)) {
                return false;
            }
            out[static_cast<size_t>(ch)].resize(produced);
        }
        // All channels produce identical frame counts (same ratio).
        out_frames = out[0].size();
        for (int ch = 1; ch < new_channels; ++ch) {
            out[static_cast<size_t>(ch)].resize(out_frames);
        }
    } else {
        out = std::move(adjusted);
    }

    // Reinterleave.
    std::vector<float> interleaved(out_frames *
                                   static_cast<size_t>(new_channels));
    for (size_t f = 0; f < out_frames; ++f) {
        for (int ch = 0; ch < new_channels; ++ch) {
            interleaved[f * static_cast<size_t>(new_channels) +
                        static_cast<size_t>(ch)] =
                out[static_cast<size_t>(ch)][f];
        }
    }

    audio_buffer_.samples = std::move(interleaved);
    audio_buffer_.sample_rate = new_rate;
    audio_buffer_.channels = new_channels;
    audio_buffer_.frame_count = out_frames;
    return true;
}

void AudioPlayer::CleanupWASAPI() {
    if (render_client_) {
        render_client_->Release();
        render_client_ = nullptr;
    }
    if (audio_client_) {
        audio_client_->Release();
        audio_client_ = nullptr;
    }
    if (device_) {
        device_->Release();
        device_ = nullptr;
    }
    if (device_enumerator_) {
        device_enumerator_->Release();
        device_enumerator_ = nullptr;
    }
    CoUninitialize();
}

bool AudioPlayer::LoadFromFile(const std::string& file_path, std::string* error_out) {
    std::lock_guard<std::recursive_mutex> lock(control_mutex_);
    XER::Audio::Core::AudioSourceLoadOptions options;
    options.input_path = file_path;
    options.target_sample_rate = sample_rate_;
    options.target_channels = channel_count_;

    XER::Audio::Core::AudioSourceBuffer buffer;
    if (!XER::Audio::Core::AudioSourceLoader::Load(options, &buffer, error_out)) {
        return false;
    }

    // Join the feed thread BEFORE swapping buffers: the render loop holds
    // pointers into audio_buffer_/pending_queue_ and a stale
    // playback_position_ can exceed the new buffer — swapping under it is
    // a data race (and an out-of-bounds read).
    const bool was_playing = state_.load() == PlaybackState::kPlaying;
    StopFeedThread();
    if (was_playing) {
        UpdateState(PlaybackState::kStopped);
    }

    audio_buffer_ = std::move(buffer);
    playback_position_ = 0;
    current_time_ = 0.0;

    return true;
}

bool AudioPlayer::LoadFromBuffer(XER::Audio::Core::AudioSourceBuffer&& buffer) {
    std::lock_guard<std::recursive_mutex> lock(control_mutex_);
    const bool was_playing = state_.load() == PlaybackState::kPlaying;
    StopFeedThread();
    if (was_playing) {
        UpdateState(PlaybackState::kStopped);
    }

    audio_buffer_ = std::move(buffer);
    playback_position_ = 0;
    current_time_ = 0.0;

    return IsLoaded();
}

bool AudioPlayer::Play() {
    std::lock_guard<std::recursive_mutex> lock(control_mutex_);
    if (!IsLoaded() || !initialized_ || audio_client_ == nullptr ||
        render_client_ == nullptr) {
        return false;
    }

    if (state_ == PlaybackState::kPlaying) {
        return true;
    }

    return StartRendering();
}

bool AudioPlayer::StartRendering() {
    // control_mutex_ held by caller (Play or the device-change rebind).
    HRESULT hr = audio_client_->Start();
    if (FAILED(hr)) {
        UpdateState(PlaybackState::kError);
        return false;
    }

    stop_thread_ = false;
    audio_thread_ = CreateThread(nullptr, 0, AudioThreadProc, this, 0, nullptr);
    if (!audio_thread_) {
        audio_client_->Stop();
        UpdateState(PlaybackState::kError);
        return false;
    }

    UpdateState(PlaybackState::kPlaying);
    return true;
}

bool AudioPlayer::Pause() {
    std::lock_guard<std::recursive_mutex> lock(control_mutex_);
    if (state_ != PlaybackState::kPlaying || !initialized_) {
        return false;
    }

    stop_thread_ = true;
    if (audio_thread_) {
        WaitForSingleObject(audio_thread_, INFINITE);
        CloseHandle(audio_thread_);
        audio_thread_ = nullptr;
    }

    HRESULT hr = audio_client_ != nullptr ? audio_client_->Stop() : S_OK;
    if (FAILED(hr)) {
        return false;
    }

    UpdateState(PlaybackState::kPaused);
    return true;
}

void AudioPlayer::StopFeedThread() {
    // Caller holds control_mutex_. The feed thread is joined before return
    // so buffer/pending swaps afterwards cannot race the render loop.
    stop_thread_ = true;
    if (audio_thread_) {
        WaitForSingleObject(audio_thread_, INFINITE);
        CloseHandle(audio_thread_);
        audio_thread_ = nullptr;
    }
    if (audio_client_) {
        audio_client_->Stop();
    }
    ClearPendingBuffer();
}

bool AudioPlayer::Stop() {
    std::lock_guard<std::recursive_mutex> lock(control_mutex_);
    if (state_ == PlaybackState::kStopped) {
        return true;
    }

    StopFeedThread();

    playback_position_ = 0;
    current_time_ = 0.0;
    UpdateState(PlaybackState::kStopped);

    return true;
}

bool AudioPlayer::Seek(double time_seconds) {
    std::lock_guard<std::recursive_mutex> lock(control_mutex_);
    if (!IsLoaded()) {
        return false;
    }

    double duration = GetDuration();
    if (time_seconds < 0.0) {
        time_seconds = 0.0;
    } else if (time_seconds > duration) {
        time_seconds = duration;
    }

    size_t target_frame = static_cast<size_t>(time_seconds * audio_buffer_.sample_rate);
    playback_position_ = target_frame * audio_buffer_.channels;
    current_time_ = time_seconds;

    return true;
}

double AudioPlayer::GetDuration() const {
    if (!IsLoaded()) {
        return 0.0;
    }
    return static_cast<double>(audio_buffer_.frame_count) / audio_buffer_.sample_rate;
}

void AudioPlayer::SetVolume(float volume) {
    if (volume < 0.0f) volume = 0.0f;
    if (volume > 1.0f) volume = 1.0f;
    volume_ = volume;
}

uint64_t AudioPlayer::FadeFrames(double seconds) const {
    if (seconds <= 0.0 || !IsLoaded()) {
        return 0;
    }
    const uint64_t frames = static_cast<uint64_t>(
        seconds * static_cast<double>(audio_buffer_.sample_rate));
    const uint64_t half = audio_buffer_.frame_count / 2;
    return frames > half ? half : frames;
}

void AudioPlayer::SetPendingBuffer(
    XER::Audio::Core::AudioSourceBuffer&& buffer, int64_t track_tag) {
    {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        pending_staging_.push_back(
            PendingEntry{std::move(buffer), track_tag});
    }
    pending_commit_.store(true);
}

void AudioPlayer::ClearPendingBuffer() {
    {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        pending_staging_.clear();
        pending_queue_.clear();
    }
    pending_position_ = 0;
    pending_count_.store(0);
    pending_commit_.store(false);
}

void AudioPlayer::ProcessAudio() {
    if (audio_client_ == nullptr || render_client_ == nullptr) {
        UpdateState(PlaybackState::kError);
        return;
    }
    // Flush denormals to zero on this thread: IIR filters and reverb tails
    // generate denormal floats that cause massive slowdowns (audible
    // crackling) on processors without FTZ/DAZ enabled by default.
    unsigned int cw = 0;
    _controlfp_s(&cw, _DN_FLUSH, _MCW_DN);

    // Per-buffer render state. The buffer can change mid-playback when a
    // queued pending buffer is promoted at a track boundary, so these are
    // refreshed by try_promote_pending().
    const std::vector<float>* samples = &audio_buffer_.samples;
    size_t total_samples = samples->size();
    uint64_t fade_total_frames = audio_buffer_.frame_count;
    uint64_t fade_in_frames = FadeFrames(fade_in_seconds_);
    uint64_t fade_out_frames = FadeFrames(fade_out_seconds_);
    bool fading = (fade_in_frames | fade_out_frames) != 0;
    uint64_t fade_channels =
        static_cast<uint64_t>((std::max)(1, audio_buffer_.channels));

    // Queue-front render state — refreshed every render iteration and by
    // try_promote_pending() when the front is consumed.
    const std::vector<float>* pending_samples = nullptr;
    size_t pending_total_samples = 0;

    // Promotes the queue front when the current buffer is exhausted.
    // Requires matching rate/channel layout; returns false otherwise.
    // After a promote the next queued buffer becomes the crossfade source,
    // so transitions chain without gaps.
    auto try_promote_pending = [&]() -> bool {
        // Skip poison entries (empty buffers queued for undecodable
        // tracks) so play order stays contiguous without audible gaps.
        while (!pending_queue_.empty() &&
               pending_queue_.front().buffer.samples.empty()) {
            pending_queue_.pop_front();
            pending_count_.store(pending_queue_.size());
        }
        if (pending_queue_.empty() ||
            pending_queue_.front().buffer.sample_rate !=
                audio_buffer_.sample_rate ||
            pending_queue_.front().buffer.channels !=
                audio_buffer_.channels) {
            return false;
        }
        PendingEntry promoted = std::move(pending_queue_.front());
        pending_queue_.pop_front();
        pending_count_.store(pending_queue_.size());
        audio_buffer_ = std::move(promoted.buffer);
        // A pending buffer that already mixed during the overlap window
        // had its fade-in performed by the crossfade ramp — don't apply
        // the track's own fade-in on top (would cause a gain dip when
        // fade_in > fade_out).
        const bool had_crossfade = pending_position_ > 0;
        playback_position_ = pending_position_;
        pending_position_ = 0;  // cursor restarts for the new front
        samples = &audio_buffer_.samples;
        total_samples = samples->size();
        fade_total_frames = audio_buffer_.frame_count;
        fade_in_frames = FadeFrames(fade_in_seconds_);
        fade_out_frames = FadeFrames(fade_out_seconds_);
        if (had_crossfade) {
            fade_in_frames = 0;
        }
        fading = (fade_in_frames | fade_out_frames) != 0;
        fade_channels =
            static_cast<uint64_t>((std::max)(1, audio_buffer_.channels));
        pending_samples = pending_queue_.empty()
            ? nullptr
            : &pending_queue_.front().buffer.samples;
        pending_total_samples =
            pending_samples != nullptr ? pending_samples->size() : 0;
        if (track_advanced_callback_) {
            track_advanced_callback_(promoted.track_tag);
        }
        return true;
    };

    while (!stop_thread_) {
        // Adopt buffers queued via SetPendingBuffer (crossfade "next"
        // tracks). The staging handoff is mutex-guarded; the flag exchange
        // keeps the audio thread off the lock in the common case.
        if (pending_commit_.exchange(false)) {
            {
                std::lock_guard<std::mutex> lock(pending_mutex_);
                while (!pending_staging_.empty()) {
                    pending_queue_.push_back(
                        std::move(pending_staging_.front()));
                    pending_staging_.pop_front();
                }
            }
            pending_count_.store(pending_queue_.size());
        }

        pending_samples = pending_queue_.empty()
            ? nullptr
            : &pending_queue_.front().buffer.samples;
        pending_total_samples =
            pending_samples != nullptr ? pending_samples->size() : 0;

        UINT32 num_padding_frames = 0;
        HRESULT hr = audio_client_->GetCurrentPadding(&num_padding_frames);
        if (FAILED(hr)) {
            break;
        }

        UINT32 num_available_frames = buffer_frame_count_ - num_padding_frames;
        if (num_available_frames == 0) {
            Sleep(10);
            continue;
        }

        BYTE* data = nullptr;
        hr = render_client_->GetBuffer(num_available_frames, &data);
        if (FAILED(hr)) {
            break;
        }

        float* output = reinterpret_cast<float*>(data);

        // When a block processor (live FX) is installed, render into a
        // reusable vector and let it transform the samples in place before
        // they are copied to the device buffer.
        const bool use_processor = static_cast<bool>(block_processor_);
        const size_t block_samples = static_cast<size_t>(num_available_frames) * channel_count_;
        if (use_processor) {
            // Exact size matters: the processor sees the whole vector.
            block_scratch_.resize(block_samples);
        }
        float* dest = use_processor ? block_scratch_.data() : output;

        for (UINT32 i = 0; i < num_available_frames; ++i) {
            // Seamless boundary: current buffer exhausted mid-block and a
            // compatible pending buffer is queued — promote and keep
            // filling this render buffer from the new track.
            if (playback_position_ >= total_samples) {
                try_promote_pending();
            }

            float frame_gain = volume_;
            float in_gain = 0.0f;
            bool mixing = false;
            if (fading) {
                // Linear "normal" curve: ramp up over the head, ramp down
                // over the tail. frame_index derives from the interleaved
                // sample position consumed so far.
                const uint64_t frame_index =
                    playback_position_ / fade_channels;
                if (frame_index < fade_in_frames) {
                    frame_gain *= static_cast<float>(
                        static_cast<double>(frame_index) /
                        static_cast<double>(fade_in_frames));
                }
                if (fade_out_frames != 0 &&
                    frame_index + fade_out_frames > fade_total_frames) {
                    const uint64_t remaining =
                        fade_total_frames > frame_index
                            ? fade_total_frames - frame_index
                            : 0;
                    frame_gain *= static_cast<float>(
                        static_cast<double>(remaining) /
                        static_cast<double>(fade_out_frames));
                    // Crossfade window == fade-out window: mix the queue
                    // front in with a complementary 0->1 ramp.
                    mixing = pending_samples != nullptr &&
                        pending_queue_.front().buffer.sample_rate ==
                            audio_buffer_.sample_rate &&
                        pending_queue_.front().buffer.channels ==
                            audio_buffer_.channels;
                }
                if (mixing) {
                    // First frame of the overlap window: rewind the pending
                    // cursor. Evaluated per-frame (not via a sticky flag) so
                    // pause/resume mid-crossfade keeps the pending position
                    // instead of restarting the next track's fade-in.
                    const uint64_t window_start =
                        fade_total_frames - fade_out_frames;
                    if (frame_index == window_start) {
                        pending_position_ = 0;
                    }
                    const uint64_t pending_frames =
                        pending_position_ / fade_channels;
                    in_gain = static_cast<float>(
                        static_cast<double>(pending_frames) /
                        static_cast<double>(fade_out_frames));
                    if (in_gain > 1.0f) {
                        in_gain = 1.0f;
                    }
                }
            }
            for (int ch = 0; ch < channel_count_; ++ch) {
                if (playback_position_ < total_samples) {
                    float sample = (*samples)[playback_position_] * frame_gain;
                    if (mixing && pending_position_ < pending_total_samples) {
                        sample += (*pending_samples)[pending_position_] *
                                  in_gain * volume_;
                    }
                    dest[i * channel_count_ + ch] = sample;
                    ++playback_position_;
                    if (mixing && pending_position_ < pending_total_samples) {
                        ++pending_position_;
                    }
                } else {
                    dest[i * channel_count_ + ch] = 0.0f;
                }
            }
        }

        if (use_processor) {
            block_processor_(block_scratch_);
            const size_t copy_count = (std::min)(block_scratch_.size(), block_samples);
            std::copy(block_scratch_.begin(),
                      block_scratch_.begin() + static_cast<std::ptrdiff_t>(copy_count), output);
            for (size_t i = copy_count; i < block_samples; ++i) {
                output[i] = 0.0f;
            }
        }

        render_client_->ReleaseBuffer(num_available_frames, 0);

        // Update time
        current_time_ = static_cast<double>(playback_position_ / channel_count_) / audio_buffer_.sample_rate;

        // Check if playback finished — a queued pending buffer promotes
        // first so the boundary stays seamless.
        if (playback_position_ >= total_samples) {
            if (try_promote_pending()) {
                continue;
            }
            UpdateState(PlaybackState::kStopped);
            if (audio_client_) {
                audio_client_->Stop();
            }
            playback_position_ = 0;
            current_time_ = 0.0;
            break;
        }

        // Notify position callback
        if (position_callback_) {
            position_callback_(current_time_, GetDuration());
        }

        Sleep(10);
    }
}

void AudioPlayer::UpdateState(PlaybackState new_state) {
    if (state_.load() != new_state) {
        state_.store(new_state);
        if (state_callback_) {
            state_callback_(new_state);
        }
    }
}

}  // namespace XER::Content::Audio
