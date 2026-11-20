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
// Classification notes: streams flagged as embedded cover art (id3v2
// APIC / FLAC picture block / mp4 covr — a "cover" metadata marker or a
// single-frame image-codec stream next to an audio stream) never count
// as the primary video stream, so a tagged mp3/m4a reports
// width/height = 0 + has_audio = true — i.e. audio, not video.
bool ProbeVideo(const std::string& path, VideoInfo* info,
                std::string* error_out = nullptr);

}  // namespace XER::Content::Video
