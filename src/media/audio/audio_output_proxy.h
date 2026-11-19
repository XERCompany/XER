/**
 * AudioOutputProxy — indirection inserted between a renderer-side client
 * and the (possibly remote) real output stream. Buffers control commands
 * until the real stream attaches; used by AudioOutputDevice.
 */
#pragma once

#include <memory>
#include <string>

#include "media/audio/audio_output_stream.h"

namespace XER::Media {

class AudioOutputProxy final : public AudioOutputStream {
public:
    // Attach/detach the real stream (audio service side).
    void Attach(std::unique_ptr<AudioOutputStream> real);
    void Detach();
    bool attached() const { return real_ != nullptr; }

    bool Open(const AudioParameters& params, std::string* error_out) override;
    void Start(AudioSourceCallback* source) override;
    void Stop() override;
    void SetVolume(double volume) override;
    void Close() override;

private:
    std::unique_ptr<AudioOutputStream> real_;
    AudioParameters params_;
    AudioSourceCallback* source_ = nullptr;
    double volume_ = 1.0;
    bool playing_ = false;
};

}  // namespace XER::Media
