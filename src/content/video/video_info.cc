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

// Audio-only demuxer names (MediaInfo::format_name is the comma-
// separated iformat alias list, e.g. "mp3" or "mov,mp4,m4a,3gp,3g2,
// mj2"). These containers physically cannot carry a real video
// track — any "video" stream they surface is embedded art. The
// ambiguous families are deliberately ABSENT: mov/mp4/m4a muxes real
// video, matroska/webm carries mka next to mkv, ogg carries theora,
// asf carries wmv.
bool IsAudioPrimaryFormat(const std::string& format_name) {
    static constexpr const char* kAudioFormats[] = {
        "mp3",   "mp2",  "mp1",  "flac",  "wav",   "w64",
        "aiff",  "aif",  "aifc", "ape",   "wv",    "tta",
        "aac",   "adts", "ac3",  "eac3",  "dts",   "dtshd",
        "mlp",   "truehd", "caf", "oma",  "aa",    "amr",
        "au",    "voc",  "mpc",  "mpc8",  "tak",   "shn",
        "spx",
    };
    std::size_t pos = 0;
    while (pos <= format_name.size()) {
        const std::size_t comma = format_name.find(',', pos);
        const std::size_t end =
            comma == std::string::npos ? format_name.size() : comma;
        const std::string token = format_name.substr(pos, end - pos);
        for (const char* name : kAudioFormats) {
            if (token == name) {
                return true;
            }
        }
        if (comma == std::string::npos) {
            break;
        }
        pos = comma + 1;
    }
    return false;
}

}  // namespace

// Embedded cover art shows up as a "video" stream in audio files.
// The bridge now surfaces the disposition flag, and the probe layers
// the remaining tell-tale signs so a stream that dodges one marker
// is still caught — an mp3 whose cover lacks BOTH the attached_pic
// flag and "cover" metadata was a real-world miss (bug report
// 2026-11-21):
//   * disposition AV_DISPOSITION_ATTACHED_PIC (bridge StreamInfo::
//     attached_pic) — the authoritative marker,
//   * stream metadata mentioning "cover" (id3v2 APIC / FLAC picture
//     block / mp4 "covr" all surface a comment like "Cover (front)"),
//   * ANY picture stream in an audio-only container (mp3/flac/wav-
//     family demuxers cannot carry a real video track — whatever
//     picture appears there is art regardless of codec or frame
//     count; this is what catches a cover written without the
//     disposition flag),
//   * or a short (1-2 frame) image-codec clip next to audio in a
//     general container — a real video has many frames, and an
//     unknown/0 count is NOT treated as art there (camera MJPEG
//     footage with uncounted frames must stay video).
// The audio gate throughout keeps a lone PNG/JPEG (a real still)
// counting as video.
bool IsAttachedPictureStream(
    const XER::Bridge::FFmpeg::StreamInfo& stream,
    bool has_audio_stream,
    const std::string& container_format) {
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
    if (!has_audio_stream) {
        return false;
    }
    if (stream.attached_pic) {
        return true;
    }
    if (IsAudioPrimaryFormat(container_format)) {
        return true;
    }
    return IsImageCodecName(stream.codec_name) &&
           stream.frame_count >= 1 && stream.frame_count <= 2;
}

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
            !IsAttachedPictureStream(stream, audio != nullptr,
                                     media.format_name)) {
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
