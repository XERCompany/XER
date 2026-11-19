/**
 * AudioSyncWriter — service-side write end paired with AudioSyncReader
 * (design doc). Transports: SharedMemoryChannel (AttachChannel, real
 * cross-process from src/cache) or SharedAudioSegment fallback.
 */
#pragma once

#include <memory>

#include "media/audio/audio_parameters.h"
#include "media/audio/shared_audio_segment.h"

namespace XER::Cache {
class SharedMemoryChannel;
}

namespace XER::Media {

class AudioSyncWriter {
public:
    explicit AudioSyncWriter(const AudioParameters& params);
    ~AudioSyncWriter();  // out-of-line: channel_ is incomplete here

    // Same-process segment path (creator side).
    bool Map(const std::string& name, std::string* error_out) {
        return segment_.Map(
            name, static_cast<std::uint32_t>(params_.GetBytesPerBuffer()),
            true, error_out);
    }
    // Cross-process channel path (host side).
    bool AttachChannel(const std::string& name, std::string* error_out);

    // Publishes |bytes| (<= buffer size) to the reader.
    bool Write(const std::uint8_t* src, std::size_t bytes);
    SharedAudioSegment* segment() { return &segment_; }

private:
    AudioParameters params_;
    SharedAudioSegment segment_;
    std::unique_ptr<XER::Cache::SharedMemoryChannel> channel_;
};

}  // namespace XER::Media
