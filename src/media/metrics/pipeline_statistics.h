/**
 * PipelineStatistics — counters/meters the pipeline reports to the
 * host (design doc: bytes read, frames decoded/rendered/dropped).
 */
#pragma once

#include <atomic>
#include <cstdint>

namespace XER::Media {

struct PipelineStatistics {
    std::atomic<std::uint64_t> audio_bytes_decoded{0};
    std::atomic<std::uint64_t> video_bytes_decoded{0};
    std::atomic<std::uint64_t> audio_frames_decoded{0};
    std::atomic<std::uint64_t> audio_frames_rendered{0};
    std::atomic<std::uint64_t> video_frames_decoded{0};
    std::atomic<std::uint64_t> video_frames_rendered{0};
    std::atomic<std::uint64_t> video_frames_dropped{0};

    void Reset() {
        audio_bytes_decoded = video_bytes_decoded = 0;
        audio_frames_decoded = audio_frames_rendered = 0;
        video_frames_decoded = video_frames_rendered =
            video_frames_dropped = 0;
    }
};

}  // namespace XER::Media
