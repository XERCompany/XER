/**
 * AudioInputDevice — AudioCapturerSource implementation that wraps a
 * platform AudioInputStream: the stream's push callback is converted to
 * CapturedAudioBuffer (f32 interleaved) for capturer clients.
 */
#pragma once

#include <memory>

#include "media/audio/audio_input_stream.h"
#include "media/audio/audio_parameters.h"
#include "media/capture/capture.h"

namespace XER::Media {

class AudioInputDevice final : public AudioCapturerSource,
                               public AudioInputStream::AudioInputCallback {
public:
    explicit AudioInputDevice(AudioParameters params = {});

    // AudioCapturerSource
    bool Start(Callback cb, std::string* error_out) override;
    void Stop() override;

    // AudioInputStream::AudioInputCallback
    void OnData(const std::uint8_t* data, int frames,
                const AudioParameters& params) override;
    void OnError(const std::string&) override {}

    // Injected stream factory for tests / alternate backends.
    void SetStream(std::unique_ptr<AudioInputStream> stream);

private:
    AudioParameters params_;
    Callback cb_;
    std::unique_ptr<AudioInputStream> stream_;
};

}  // namespace XER::Media
