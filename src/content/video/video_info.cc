#include "video_info.h"

#include <cctype>
#include <filesystem>
#include <string>
#include <system_error>

#include "helper/string_utils.h"
#include "wrapper/ffmpeg/ffmpeg_engine_bridge.h"

namespace XER::Content::Video {
namespace {

// Image-codec "video" streams: a single-frame stream in one of these
// is a still picture, not movie frames. Consulted ONLY alongside an
// audio stream to spot embedded album art — a tagged mp3/flac/m4a
// exposes its cover as exactly such a stream.
bool IsImageCodecName(const std::string& codec_name) {
    static constexpr const char* kImageCodecs[] = {
        "png",      "mjpeg", "bmp",  "webp",    "tiff",  "targa",
        "gif",      "ppm",   "pgm",  "pbm",     "pam",   "xpm",
        "xwd",      "dds",   "sgi",  "psd",     "ico",   "qoi",
        "jpeg2000", "jpegls", "exr", "dpx",     "sunrast", "avif",
        "heic",
    };
    for (const char* name : kImageCodecs) {
        if (codec_name == name) {
            return true;
        }
    }
    return false;
}

// Embedded cover art shows up as a "video" stream flagged attached-
// pic — the bridge's StreamInfo drops the disposition flags, so the
// probe reads the tell-tale signs instead:
//   * stream metadata mentioning "cover" (id3v2 APIC / FLAC picture
//     block / mp4 "covr" all surface a comment like "Cover (front)"),
//   * or a single-frame image-codec stream next to an audio stream
//     (a real video has many frames or a non-image codec; the audio
//     gate keeps a lone PNG/JPEG from losing its picture).
bool IsAttachedPicture(
    const XER::Bridge::FFmpeg::StreamInfo& stream,
    bool has_audio_stream) {
    for (const auto& entry : stream.metadata) {
        std::string haystack = entry.key + "=" + entry.value;
        for (char& c : haystack) {
            c = static_cast<char>(
                std::tolower(static_cast<unsigned char>(c)));
        }
        if (haystack.find("cover") != std::string::npos) {
            return true;
        }
    }
    return has_audio_stream && stream.frame_count == 1 &&
           IsImageCodecName(stream.codec_name);
}

}  // namespace

bool ProbeVideo(const std::string& path, VideoInfo* info,
                std::string* error_out) {
    if (info == nullptr) {
        if (error_out != nullptr) *error_out = "null info";
        return false;
    }
    *info = VideoInfo{};
    info->path = path;

    XER::Bridge::FFmpeg::MediaInfo media;
    if (!XER::Bridge::FFmpeg::ProbeMedia(path, &media, error_out)) {
        return false;
    }

    const XER::Bridge::FFmpeg::StreamInfo* audio = nullptr;
    for (const auto& stream : media.streams) {
        if (audio == nullptr && stream.media_type == "audio") {
            audio = &stream;
        }
    }
    // First REAL video stream wins the primary slot: album art
    // embedded in audio files arrives as a "video" stream too, but a
    // cover is not a picture for a video track — an mp3/flac/m4a with
    // art must classify as audio.
    const XER::Bridge::FFmpeg::StreamInfo* video = nullptr;
    for (const auto& stream : media.streams) {
        if (stream.media_type == "video" &&
            !IsAttachedPicture(stream, audio != nullptr)) {
            video = &stream;
            break;
        }
    }
    if (video == nullptr) {
        if (audio == nullptr) {
            if (error_out != nullptr)
                *error_out = "no decodable audio/video streams";
            return false;
        }
        // Audio-only container: succeed with width/height = 0 —
        // matches the header contract and the decoder's audio-only
        // mode. Callers that need pictures check those fields.
    }

    if (video != nullptr) {
        info->width = video->width;
        info->height = video->height;
        info->video_codec = video->codec_name;
    }
    info->has_audio = audio != nullptr;
    info->audio_codec = audio != nullptr ? audio->codec_name : "";

    // Container duration is in AV_TIME_BASE units (microseconds); the
    // per-stream fallback uses the video stream, or the audio stream
    // for audio-only containers.
    const auto* duration_stream = video != nullptr ? video : audio;
    if (media.duration > 0) {
        info->duration_seconds =
            static_cast<double>(media.duration) / 1000000.0;
    } else if (duration_stream != nullptr && duration_stream->duration > 0 &&
               duration_stream->time_base_den > 0) {
        info->duration_seconds =
            static_cast<double>(duration_stream->duration) *
            duration_stream->time_base_num /
            static_cast<double>(duration_stream->time_base_den);
    }
    // Frame-rate estimate from the video stream header; the decoder
    // reports the authoritative value once opened. Audio-only files
    // keep fps = 0.
    if (video != nullptr && video->frame_count > 0 &&
        info->duration_seconds > 0.0) {
        info->fps = video->frame_count / info->duration_seconds;
    }

    std::error_code ec;
    const auto size = std::filesystem::file_size(
        std::filesystem::path(XER::Helper::String::Utf8ToWide(path)), ec);
    if (!ec) {
        info->file_size_bytes = static_cast<std::int64_t>(size);
    }

    info->valid = true;
    return true;
}

}  // namespace XER::Content::Video
