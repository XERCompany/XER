// Platform audio-sink factory for the VideoEditor preview engine.
// Returns the OS-default render backend as an IAudioSink — WASAPI
// shared-mode on Windows (playback/wasapi_sink.*). Keeping this a
// factory (rather than having callers new WasapiSink) keeps the exe/UI
// code platform-clean: they include only this header and audio_output.h.
#pragma once

#include <memory>
#include <string>

#include "audio_output.h"

namespace XER::VideoEditor {

// Creates the platform default audio sink.
//
// Windows: a WasapiSink eagerly opened at the engine's default mixer
// rate (48000 Hz, stereo s16). Returns nullptr + *error_out if no
// render endpoint is available (no audio device, remote session, etc.)
// — callers should treat nullptr as "audio disabled", not fatal.
// If the engine is configured for a different rate the sink re-
// negotiates on the first Write (see wasapi_sink.h).
//
// Other platforms: no backend yet — returns nullptr + *error_out.
std::unique_ptr<IAudioSink> MakePlatformAudioSink(
    std::string* error_out = nullptr);

}  // namespace XER::VideoEditor
