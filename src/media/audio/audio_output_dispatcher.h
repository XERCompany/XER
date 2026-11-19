/**
 * AudioOutputDispatcher — reuses physical output streams across clients
 * with identical AudioParameters (design doc: sits between clients and
 * physical streams, handles stream reuse).
 */
#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>

#include "media/audio/audio_output_stream.h"

namespace XER::Media {

class AudioOutputDispatcher {
public:
    // Factory producing physical streams (AudioManager supplies this).
    using StreamFactory =
        std::function<std::unique_ptr<AudioOutputStream>(
            const AudioParameters&, std::string*)>;

    explicit AudioOutputDispatcher(StreamFactory factory)
        : factory_(std::move(factory)) {}

    // Returns a shared stream for |params|, opening one on first use.
    AudioOutputStream* Acquire(const AudioParameters& params,
                               std::string* error_out);
    void Release(const AudioParameters& params);
    std::size_t open_count() const { return streams_.size(); }

private:
    struct Entry {
        std::unique_ptr<AudioOutputStream> stream;
        int refs = 0;
    };
    StreamFactory factory_;
    std::map<std::string, Entry> streams_;  // key: params signature
};

}  // namespace XER::Media
