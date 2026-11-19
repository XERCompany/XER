/**
 * AudioSyncReader — renderer-side read end of the synchronous audio
 * channel (design doc). Transport options, tried in order:
 *   1. XER::Cache::SharedMemoryChannel (AttachChannel) — real
 *      cross-process message channel from src/cache.
 *   2. SharedAudioSegment polling (Map) — same-process fallback.
 */
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include "media/audio/audio_parameters.h"
#include "media/audio/shared_audio_segment.h"

namespace XER::Cache {
class SharedMemoryChannel;
}

namespace XER::Media {

class AudioSyncReader {
public:
    explicit AudioSyncReader(const AudioParameters& params);
    ~AudioSyncReader();

    // Same-process segment path.
    bool Map(const std::string& name, std::string* error_out);
    // Cross-process channel path (peer side).
    bool AttachChannel(const std::string& name, std::string* error_out);

    // Blocks until the next block is ready or |timeout_ms| elapses;
    // returns frames copied into |dest| (must fit frames_per_buffer),
    // 0 on timeout, -1 on error.
    int Read(std::uint8_t* dest, int timeout_ms);
    void SignalError();

    const AudioParameters& params() const { return params_; }

private:
    AudioParameters params_;
    SharedAudioSegment segment_;
    std::unique_ptr<XER::Cache::SharedMemoryChannel> channel_;
    std::atomic<bool> error_{false};
};

}  // namespace XER::Media
