/**
 * AudioOutputDevice — AudioRendererSink backed by an AudioOutputProxy →
 * AudioManager stream (design doc: sink invokes its callback
 * periodically when new audio frames are required).
 */
#pragma once

#include <memory>
#include <vector>

#include "media/audio/audio_output_proxy.h"
#include "media/audio/audio_parameters.h"
#include "media/base/audio_bus.h"
#include "media/base/audio_clock.h"
#include "media/sinks/audio_renderer_sink.h"

namespace XER::Media {

class AudioOutputDevice final : public AudioRendererSink,
                                public AudioOutputStream::AudioSourceCallback {
public:
    explicit AudioOutputDevice(AudioParameters params = {});
    ~AudioOutputDevice() override;

    // AudioRendererSink
    void Start(Callback cb) override;
    void Stop() override;
    void Play() override;
    void Pause() override;
    void SetVolume(double volume) override;

    // AudioSourceCallback (device thread — pull model)
    int OnMoreData(std::uint8_t* dest, int frames,
                   const AudioParameters& params) override;
    void OnError(const std::string&) override {}

    // Binds the renderer's PCM source (AudioRendererImpl::RenderInto)
    // and the master clock: consumed frames advance audio time (design
    // doc: audio-master A/V sync).
    void SetRenderSource(std::function<int(AudioBus*, int)> source,
                         AudioClock* clock = nullptr);

private:
    AudioParameters params_;
    AudioOutputProxy proxy_;
    Callback sink_cb_;
    std::function<int(AudioBus*, int)> render_source_;
    AudioClock* clock_ = nullptr;
    AudioBus pull_bus_ = AudioBus::Create(2, 8192);
    bool playing_ = false;
    bool requesting_ = false;
};

}  // namespace XER::Media
