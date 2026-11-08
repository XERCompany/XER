#include "content/audio/audio_player.h"

#include <algorithm>
#include <cmath>
#include <cfloat>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <thread>

namespace XER::Content::Audio {

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
    , initialized_(false)
    , audio_thread_(nullptr)
    , stop_thread_(false) {
}

AudioPlayer::~AudioPlayer() {
    Stop();
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

    hr = device_->Activate(
        __uuidof(IAudioClient),
        CLSCTX_ALL,
        nullptr,
        reinterpret_cast<void**>(&audio_client_)
    );

    if (FAILED(hr)) {
        device_->Release();
        device_ = nullptr;
        device_enumerator_->Release();
        device_enumerator_ = nullptr;
        CoUninitialize();
        return false;
    }

    WAVEFORMATEX* mix_format = nullptr;
    hr = audio_client_->GetMixFormat(&mix_format);
    if (FAILED(hr)) {
        audio_client_->Release();
        audio_client_ = nullptr;
        device_->Release();
        device_ = nullptr;
        device_enumerator_->Release();
        device_enumerator_ = nullptr;
        CoUninitialize();
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
        device_->Release();
        device_ = nullptr;
        device_enumerator_->Release();
        device_enumerator_ = nullptr;
        CoUninitialize();
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
        device_->Release();
        device_ = nullptr;
        device_enumerator_->Release();
        device_enumerator_ = nullptr;
        CoUninitialize();
        return false;
    }

    hr = audio_client_->GetService(
        __uuidof(IAudioRenderClient),
        reinterpret_cast<void**>(&render_client_)
    );

    if (FAILED(hr)) {
        audio_client_->Release();
        audio_client_ = nullptr;
        device_->Release();
        device_ = nullptr;
        device_enumerator_->Release();
        device_enumerator_ = nullptr;
        CoUninitialize();
        return false;
    }

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
    XER::Audio::Core::AudioSourceLoadOptions options;
    options.input_path = file_path;
    options.target_sample_rate = sample_rate_;
    options.target_channels = channel_count_;

    XER::Audio::Core::AudioSourceBuffer buffer;
    if (!XER::Audio::Core::AudioSourceLoader::Load(options, &buffer, error_out)) {
        return false;
    }

    audio_buffer_ = std::move(buffer);
    playback_position_ = 0;
    current_time_ = 0.0;

    if (state_ == PlaybackState::kPlaying) {
        Stop();
    }

    return true;
}

bool AudioPlayer::Play() {
    if (!IsLoaded() || !initialized_) {
        return false;
    }

    if (state_ == PlaybackState::kPlaying) {
        return true;
    }

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
    if (state_ != PlaybackState::kPlaying || !initialized_) {
        return false;
    }

    stop_thread_ = true;
    if (audio_thread_) {
        WaitForSingleObject(audio_thread_, INFINITE);
        CloseHandle(audio_thread_);
        audio_thread_ = nullptr;
    }

    HRESULT hr = audio_client_->Stop();
    if (FAILED(hr)) {
        return false;
    }

    UpdateState(PlaybackState::kPaused);
    return true;
}

bool AudioPlayer::Stop() {
    if (state_ == PlaybackState::kStopped) {
        return true;
    }

    stop_thread_ = true;
    if (audio_thread_) {
        WaitForSingleObject(audio_thread_, INFINITE);
        CloseHandle(audio_thread_);
        audio_thread_ = nullptr;
    }

    if (audio_client_) {
        audio_client_->Stop();
    }

    playback_position_ = 0;
    current_time_ = 0.0;
    UpdateState(PlaybackState::kStopped);

    return true;
}

bool AudioPlayer::Seek(double time_seconds) {
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

void AudioPlayer::ProcessAudio() {
    // Flush denormals to zero on this thread: IIR filters and reverb tails
    // generate denormal floats that cause massive slowdowns (audible
    // crackling) on processors without FTZ/DAZ enabled by default.
    unsigned int cw = 0;
    _controlfp_s(&cw, _DN_FLUSH, _MCW_DN);

    while (!stop_thread_) {
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
        const std::vector<float>& samples = audio_buffer_.samples;
        size_t total_samples = samples.size();

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
            for (int ch = 0; ch < channel_count_; ++ch) {
                if (playback_position_ < total_samples) {
                    float sample = samples[playback_position_] * volume_;
                    dest[i * channel_count_ + ch] = sample;
                    ++playback_position_;
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

        // Check if playback finished
        if (playback_position_ >= total_samples) {
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
    if (state_ != new_state) {
        state_ = new_state;
        if (state_callback_) {
            state_callback_(state_);
        }
    }
}

}  // namespace XER::Content::Audio
