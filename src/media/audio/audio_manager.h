/**
 * AudioManager — manages creation of AudioOutputStream / AudioInputStream
 * instances, routes streams across hardware devices, handles mixing and
 * stream reuse (design doc). On Windows it bridges to the existing
 * XER::Audio engine / WASAPI backend.
 */
#pragma once

#include <memory>
#include <string>

#include "media/audio/audio_input_stream.h"
#include "media/audio/audio_output_dispatcher.h"
#include "media/audio/audio_output_stream.h"

namespace XER::Media {

class AudioManager {
public:
    static AudioManager* Get();  // process-wide singleton

    // Creates a platform stream for |params|. Default impl delegates to
    // the WASAPI engine via wasapi_output_stream.
    std::unique_ptr<AudioOutputStream> MakeOutputStream(
        const AudioParameters& params, std::string* error_out);
    std::unique_ptr<AudioInputStream> MakeInputStream(
        const AudioParameters& params, std::string* error_out);

    // Shared-stream dispatch (reuse across clients, per doc).
    AudioOutputStream* AcquireSharedOutput(const AudioParameters& params,
                                           std::string* error_out) {
        return dispatcher_.Acquire(params, error_out);
    }
    void ReleaseSharedOutput(const AudioParameters& params) {
        dispatcher_.Release(params);
    }

    AudioParameters DefaultOutputParameters() const { return defaults_; }
    void SetDefaultOutputParameters(const AudioParameters& p) {
        defaults_ = p;
    }

private:
    AudioManager();
    AudioParameters defaults_;
    AudioOutputDispatcher dispatcher_;
};

}  // namespace XER::Media
