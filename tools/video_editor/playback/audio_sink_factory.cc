// See audio_sink_factory.h — platform default IAudioSink selection.

#include "audio_sink_factory.h"

#include "wasapi_sink.h"

namespace XER::VideoEditor {

std::unique_ptr<IAudioSink> MakePlatformAudioSink(std::string* error_out) {
#ifdef _WIN32
    // 48000 = PreviewOptions::sample_rate default; eager open so device
    // problems surface here as nullptr+error rather than mid-playback.
    return MakeWasapiSink(48000, error_out);
#else
    if (error_out != nullptr) {
        *error_out = "no platform audio sink backend on this OS";
    }
    return nullptr;
#endif
}

}  // namespace XER::VideoEditor
