#include "app/app_audio_controller.h"

#include <algorithm>
#include <cmath>

namespace XER::App {

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
    // Route every WASAPI block through the live effect chain when active.
    audio_player_.SetBlockProcessor([this](std::vector<float>& samples) {
        ProcessAudioWithEffects(samples);
    });
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

XER::Content::Audio::PlaybackState AudioController::GetState() const {
    if (!initialized_) return XER::Content::Audio::PlaybackState::kStopped;
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

bool AudioController::ApplyEffectLive(const XER::Audio::FX::CustomEffectPackage& package, std::string* error_out) {
    if (!initialized_) {
        if (error_out) *error_out = "AudioController not initialized";
        return false;
    }

    // Build and initialize the new flow WITHOUT holding the mutex: DSP init
    // (reverb buffers, clap hosts, ...) can take tens of ms, and the audio
    // thread must keep processing the old flow meanwhile — otherwise every
    // parameter change causes a buffer underrun and audible crackling.
    auto flow = std::make_unique<XER::Audio::FX::CustomEffectFlow>();
    flow->SetStreamingMode(true);
    const float sample_rate = static_cast<float>(audio_player_.GetSampleRate());
    if (!flow->Initialize(package, sample_rate, 1024, error_out)) {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(effect_mutex_);
        effect_flow_ = std::move(flow);
        effect_enabled_ = true;
    }
    return true;
}

void AudioController::DisableEffectLive() {
    std::lock_guard<std::mutex> lock(effect_mutex_);
    effect_flow_.reset();
    effect_enabled_ = false;
}

void AudioController::SetBlockProcessor(BlockProcessor processor) {
    std::lock_guard<std::mutex> lock(effect_mutex_);
    block_processor_ = std::move(processor);
}

void AudioController::ProcessAudioWithEffects(std::vector<float>& samples) {
    std::lock_guard<std::mutex> lock(effect_mutex_);
    const bool has_fx = effect_enabled_ && effect_flow_ != nullptr;
    const bool has_plugin = static_cast<bool>(block_processor_);
    if ((!has_fx && !has_plugin) || samples.empty()) {
        return;
    }

    // CustomEffectFlow processes a mono stream; deinterleave the device
    // channels, run each through the chain and reinterleave.
    const int channels = std::max(1, audio_player_.GetChannelCount());
    const size_t frames = samples.size() / static_cast<size_t>(channels);
    if (frames == 0) {
        return;
    }

    std::vector<std::vector<float>> channel_data(
        static_cast<size_t>(channels), std::vector<float>(frames, 0.0f));
    for (int ch = 0; ch < channels; ++ch) {
        for (size_t i = 0; i < frames; ++i) {
            channel_data[static_cast<size_t>(ch)][i] =
                samples[i * static_cast<size_t>(channels) + static_cast<size_t>(ch)];
        }
    }

    bool ok = true;
    std::vector<float> plugin_scratch;
    if (has_plugin) {
        plugin_scratch.resize(frames);
    }
    for (int ch = 0; ch < channels && ok; ++ch) {
        auto& channel = channel_data[static_cast<size_t>(ch)];
        if (has_fx) {
            std::vector<float> output;
            std::string error;
            ok = effect_flow_->Process(channel, &output, &error);
            if (ok && output.size() == frames) {
                channel = std::move(output);
            }
        }
        if (ok && has_plugin) {
            // Plugin gets its own in/out buffers (in-place is not guaranteed).
            ok = block_processor_(channel.data(), plugin_scratch.data(),
                                  static_cast<uint32_t>(frames));
            if (ok) {
                std::swap(channel, plugin_scratch);
            }
        }
    }
    if (!ok) {
        return; // pass through dry on failure
    }

    for (size_t i = 0; i < frames; ++i) {
        for (int ch = 0; ch < channels; ++ch) {
            samples[i * static_cast<size_t>(channels) + static_cast<size_t>(ch)] =
                channel_data[static_cast<size_t>(ch)][i];
        }
    }

    // Soft-clip safety: EQ boosts (+15 dB), bass boost and wet effects can
    // push the signal past 0 dBFS — tanh keeps overshoots smooth instead of
    // the harsh digital clipping users hear as "noise".
    for (float& sample : samples) {
        sample = std::tanh(sample);
    }
}

}  // namespace XER::App