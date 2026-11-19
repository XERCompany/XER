#include "media/audio/wasapi_output_stream.h"

#include <cstring>

#include "audio/audio_core/audio_engine.h"

namespace XER::Media {

WasapiOutputStream::WasapiOutputStream(const AudioParameters& params)
    : params_(params) {}

WasapiOutputStream::~WasapiOutputStream() { Stop(); }

bool WasapiOutputStream::Open(const AudioParameters& params,
                              std::string* error_out) {
    params_ = params;
    if (!params_.IsValid()) {
        if (error_out) *error_out = "invalid audio parameters";
        return false;
    }
    engine_ = std::make_unique<XER::Audio::Core::AudioEngine>();
    if (!engine_->Initialize(params_.sample_rate,
                             params_.frames_per_buffer,
                             params_.channels())) {
        if (error_out) *error_out = "audio engine init failed";
        engine_.reset();
        return false;
    }
    out_planes_.resize(params_.channels());
    for (auto& p : out_planes_) p.assign(params_.frames_per_buffer, 0.0f);
    out_ptrs_.resize(params_.channels());
    for (int c = 0; c < params_.channels(); ++c)
        out_ptrs_[c] = out_planes_[c].data();
    scratch_.resize(params_.GetBytesPerBuffer());

    engine_->SetProcessCallback(
        [this](float**, float** outputs, int channels, size_t frames) {
            const int n = static_cast<int>(frames);
            // Pull interleaved f32 from the client, de-interleave into
            // the engine's planar output, apply stream volume.
            if (source_ && playing_ &&
                params_.format == SampleFormat::kF32) {
                source_->OnMoreData(scratch_.data(), n, params_);
            } else {
                std::memset(scratch_.data(), 0, scratch_.size());
            }
            const auto* inter =
                reinterpret_cast<const float*>(scratch_.data());
            for (int c = 0; c < channels; ++c)
                for (int f = 0; f < n; ++f)
                    outputs[c][f] = static_cast<float>(
                        inter[f * channels + c] * volume_);
        });
    return true;
}

void WasapiOutputStream::Start(AudioSourceCallback* source) {
    source_ = source;
    playing_ = true;
    if (!engine_) return;
    engine_->Start();
    const std::uint32_t period_us =
        static_cast<std::uint32_t>(params_.frames_per_buffer) *
        1000000u / static_cast<std::uint32_t>(params_.sample_rate);
    // Device thread: drives the engine each buffer period (the real
    // WASAPI pump replaces this clock when the engine bridge exposes
    // a hardware callback).
    device_thread_.Start(
        [this] {
            if (playing_ && engine_)
                engine_->ProcessAudio(nullptr, out_ptrs_.data(),
                                      params_.frames_per_buffer);
        },
        period_us);
}

void WasapiOutputStream::Stop() {
    playing_ = false;
    device_thread_.Stop();
    if (engine_) engine_->Stop();
}

void WasapiOutputStream::SetVolume(double volume) {
    volume_ = volume < 0.0 ? 0.0 : (volume > 1.0 ? 1.0 : volume);
}

void WasapiOutputStream::Close() { Stop(); }

std::vector<float> WasapiOutputStream::OutputLevels() const {
    return engine_ ? engine_->GetOutputLevels() : std::vector<float>{};
}

}  // namespace XER::Media
