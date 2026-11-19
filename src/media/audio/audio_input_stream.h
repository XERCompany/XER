/**
 * AudioInputStream — platform capture interface (design doc).
 * Input uses a PUSH model: AudioInputCallback::OnData() is invoked by the
 * stream with captured PCM.
 */
#pragma once

#include <cstdint>
#include <string>

#include "media/audio/audio_parameters.h"

namespace XER::Media {

class AudioInputStream {
public:
    class AudioInputCallback {
    public:
        virtual ~AudioInputCallback() = default;
        virtual void OnData(const std::uint8_t* data, int frames,
                            const AudioParameters& params) = 0;
        virtual void OnError(const std::string& message) = 0;
    };

    virtual ~AudioInputStream() = default;
    virtual bool Open(const AudioParameters& params,
                      std::string* error_out) = 0;
    virtual void Start(AudioInputCallback* sink) = 0;
    virtual void Stop() = 0;
    virtual void Close() = 0;
};

}  // namespace XER::Media
