/**
 * AudioOutputStream — platform output interface (design doc).
 * Output uses a PULL model: AudioSourceCallback::OnMoreData() is invoked
 * by the stream when it needs more PCM.
 */
#pragma once

#include "media/audio/audio_parameters.h"

namespace XER::Media {

class AudioOutputStream {
public:
    // Implemented by the client; called on the audio device thread to
    // fill |dest| with |frames| interleaved samples. Returns frames
    // actually written.
    class AudioSourceCallback {
    public:
        virtual ~AudioSourceCallback() = default;
        virtual int OnMoreData(std::uint8_t* dest, int frames,
                               const AudioParameters& params) = 0;
        virtual void OnError(const std::string& message) = 0;
    };

    virtual ~AudioOutputStream() = default;
    virtual bool Open(const AudioParameters& params,
                      std::string* error_out) = 0;
    virtual void Start(AudioSourceCallback* source) = 0;
    virtual void Stop() = 0;
    virtual void SetVolume(double volume) = 0;
    virtual void Close() = 0;
};

}  // namespace XER::Media
