#include "media/audio/audio_output_device.h"

#include <cstring>

#include "media/audio/audio_manager.h"

namespace XER::Media {

AudioOutputDevice::AudioOutputDevice(AudioParameters params)
    : params_(params) {}

AudioOutputDevice::~AudioOutputDevice() { Stop(); }

void AudioOutputDevice::Start(Callback cb) {
    sink_cb_ = std::move(cb);
    if (!proxy_.attached()) {
        proxy_.Open(params_, nullptr);
        proxy_.Attach(
            AudioManager::Get()->MakeOutputStream(params_, nullptr));
    }
    proxy_.Start(this);
}

void AudioOutputDevice::Stop() {
    requesting_ = false;
    proxy_.Stop();
    proxy_.Detach();
}

void AudioOutputDevice::Play() { playing_ = true; }
void AudioOutputDevice::Pause() { playing_ = false; }
void AudioOutputDevice::SetVolume(double volume) {
    proxy_.SetVolume(volume);
}

void AudioOutputDevice::SetRenderSource(
    std::function<int(AudioBus*, int)> source, AudioClock* clock) {
    render_source_ = std::move(source);
    clock_ = clock;
    pull_bus_ = AudioBus::Create(params_.channels(), 8192);
}

int AudioOutputDevice::OnMoreData(std::uint8_t* dest, int frames,
                                  const AudioParameters& params) {
    const std::size_t bytes = static_cast<std::size_t>(frames) *
                              params.channels() *
                              SampleFormatBytes(params.format);
    std::memset(dest, 0, bytes);
    // Real render path: pull planar f32 from the renderer, convert to
    // the device format, and advance the master clock.
    if (playing_ && render_source_) {
        const int got = render_source_(&pull_bus_, frames);
        if (got > 0) {
            pull_bus_.ToInterleavedFormat(params.format, got, dest);
            if (clock_) clock_->WroteAudio(got);
        }
        return frames;
    }
    // Notify the sink it should render the next block. The sink's
    // callback posts to the renderer thread (RT rule: no blocking work).
    if (!requesting_ && playing_ && sink_cb_) {
        requesting_ = true;
        sink_cb_();
        requesting_ = false;
    }
    return frames;
}

}  // namespace XER::Media
