#include "media/audio/audio_input_device.h"

#include "media/audio/audio_manager.h"

namespace XER::Media {

AudioInputDevice::AudioInputDevice(AudioParameters params)
    : params_(params) {}

void AudioInputDevice::SetStream(std::unique_ptr<AudioInputStream> stream) {
    stream_ = std::move(stream);
}

bool AudioInputDevice::Start(Callback cb, std::string* error_out) {
    cb_ = std::move(cb);
    if (!stream_) {
        stream_ = AudioManager::Get()->MakeInputStream(params_, error_out);
        if (!stream_) return false;
    }
    if (!stream_->Open(params_, error_out)) return false;
    stream_->Start(this);
    return true;
}

void AudioInputDevice::Stop() {
    if (stream_) {
        stream_->Stop();
        stream_->Close();
    }
}

void AudioInputDevice::OnData(const std::uint8_t* data, int frames,
                              const AudioParameters& params) {
    if (!cb_ || !data || frames <= 0) return;
    CapturedAudioBuffer buffer;
    buffer.sample_rate = params.sample_rate;
    buffer.channels = params.channels();
    buffer.timestamp = 0;
    const int count = frames * params.channels();
    buffer.samples.resize(count);
    // Convert to f32 interleaved regardless of source format.
    if (params.format == SampleFormat::kF32) {
        std::memcpy(buffer.samples.data(), data,
                    count * sizeof(float));
    } else if (params.format == SampleFormat::kS16) {
        const auto* s = reinterpret_cast<const std::int16_t*>(data);
        for (int i = 0; i < count; ++i)
            buffer.samples[i] = static_cast<float>(s[i]) / 32768.0f;
    }
    cb_(std::move(buffer));
}

}  // namespace XER::Media
