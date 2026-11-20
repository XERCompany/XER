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
// Classification notes: streams identified as embedded cover art
// (id3v2 APIC / FLAC picture block / mp4 covr) never count as the
// primary video stream, so a tagged mp3/m4a reports width/height = 0
// + has_audio = true — i.e. audio, not video. The detection layers
// the AV_DISPOSITION_ATTACHED_PIC flag, a "cover" metadata marker,
// the audio-only-container rule (mp3/flac/wav-family demuxers cannot
// carry real video — any picture stream there is art, even when the
// muxer wrote it without the attached-pic flag), and a 1-2 frame
// image-codec stream next to audio.
bool ProbeVideo(const std::string& path, VideoInfo* info,
                std::string* error_out = nullptr);

}  // namespace XER::Content::Video

namespace XER::Bridge::FFmpeg {
struct StreamInfo;   // probe region — forward-declared seam param
}

namespace XER::Content::Video {

// Test/diagnostic seam — the attached-picture (cover art) predicate
// ProbeVideo applies to each "video"-typed stream: the
// AV_DISPOSITION_ATTACHED_PIC flag, a "cover" metadata marker, ANY
// picture stream in an audio-only container, or a 1-2 frame
// image-codec stream alongside an audio stream. `container_format`
// is MediaInfo::format_name (the comma-separated demuxer alias
// list); `has_audio_stream` is the container's audio presence.
bool IsAttachedPictureStream(
    const XER::Bridge::FFmpeg::StreamInfo& stream,
    bool has_audio_stream,
    const std::string& container_format);

}  // namespace XER::Content::Video
