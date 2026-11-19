/**
 * WasapiOutputStream — AudioOutputStream driven by the real
 * XER::Audio::Core::AudioEngine (src/audio/audio_core). The engine runs
 * its process callback from the media AudioDeviceThread at buffer
 * period; the callback pulls interleaved f32 from AudioSourceCallback
 * and de-interleaves into the engine's output planes.
 */
#pragma once

#include <memory>
#include <vector>

#include "media/audio/audio_device_thread.h"
#include "media/audio/audio_output_stream.h"

namespace XER::Audio::Core {
class AudioEngine;
}

namespace XER::Media {

class WasapiOutputStream final : public AudioOutputStream {
public:
    explicit WasapiOutputStream(const AudioParameters& params);
    ~WasapiOutputStream() override;

    bool Open(const AudioParameters& params,
              std::string* error_out) override;
    void Start(AudioSourceCallback* source) override;
    void Stop() override;
    void SetVolume(double volume) override;
    void Close() override;

    // Engine levels for meters/ducking (dB-normalised 0..1 peaks).
    std::vector<float> OutputLevels() const;

private:
    AudioParameters params_;
    std::unique_ptr<XER::Audio::Core::AudioEngine> engine_;
    AudioDeviceThread device_thread_;
    AudioSourceCallback* source_ = nullptr;
    std::vector<std::vector<float>> out_planes_;
    std::vector<float*> out_ptrs_;
    std::vector<std::uint8_t> scratch_;
    double volume_ = 1.0;
    bool playing_ = false;
};

}  // namespace XER::Media
