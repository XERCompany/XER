#include "media/audio/audio_manager.h"

#include "media/audio/wasapi_output_stream.h"

namespace XER::Media {

AudioManager* AudioManager::Get() {
    static AudioManager mgr;
    return &mgr;
}

AudioManager::AudioManager()
    : dispatcher_([](const AudioParameters& p, std::string* e) {
          return AudioManager::Get()->MakeOutputStream(p, e);
      }) {}

std::unique_ptr<AudioOutputStream> AudioManager::MakeOutputStream(
    const AudioParameters& params, std::string* error_out) {
    return std::make_unique<WasapiOutputStream>(params);
}

std::unique_ptr<AudioInputStream> AudioManager::MakeInputStream(
    const AudioParameters&, std::string* error_out) {
    if (error_out)
        *error_out = "audio input via capture/AudioCapturerSource";
    return nullptr;
}

}  // namespace XER::Media
