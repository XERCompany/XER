#pragma once

#include <cstdint>
#include <string>

namespace XER::Content::Video {

// Static metadata about a media file, probed from container headers —
// no frames are decoded.
struct VideoInfo {
    std::string path;
    int width = 0;
    int height = 0;
    double fps = 0.0;               // 0 when unknown
    double duration_seconds = 0.0;  // 0 when unknown
    std::string video_codec;
    std::string audio_codec;        // empty when the file has no audio
    bool has_audio = false;
    std::int64_t file_size_bytes = 0;
    bool valid = false;
};

// Probes `path` through the FFmpeg bridge (avformat only). Returns false
// and fills error_out on failure; on success `info->valid` is true.
// An audio-only file probes successfully but reports width/height = 0 —
// callers that need pictures should check those fields.
bool ProbeVideo(const std::string& path, VideoInfo* info,
                std::string* error_out = nullptr);

}  // namespace XER::Content::Video
