#include "media/audio/audio_output_proxy.h"

namespace XER::Media {

void AudioOutputProxy::Attach(std::unique_ptr<AudioOutputStream> real) {
    real_ = std::move(real);
    if (!real_) return;
    real_->Open(params_, nullptr);
    real_->SetVolume(volume_);
    if (playing_ && source_) real_->Start(source_);
}

void AudioOutputProxy::Detach() {
    if (real_) real_->Close();
    real_.reset();
}

bool AudioOutputProxy::Open(const AudioParameters& params,
                            std::string* error_out) {
    params_ = params;
    if (real_) return real_->Open(params, error_out);
    return true;
}

void AudioOutputProxy::Start(AudioSourceCallback* source) {
    source_ = source;
    playing_ = true;
    if (real_) real_->Start(source);
}

void AudioOutputProxy::Stop() {
    playing_ = false;
    if (real_) real_->Stop();
}

void AudioOutputProxy::SetVolume(double volume) {
    volume_ = volume;
    if (real_) real_->SetVolume(volume);
}

void AudioOutputProxy::Close() {
    playing_ = false;
    if (real_) real_->Close();
}

}  // namespace XER::Media
