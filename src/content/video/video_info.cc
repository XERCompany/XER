#include "video_info.h"

#include <filesystem>
#include <system_error>

#include "helper/string_utils.h"
#include "wrapper/ffmpeg/ffmpeg_engine_bridge.h"

namespace XER::Content::Video {

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

    const XER::Bridge::FFmpeg::StreamInfo* video = nullptr;
    const XER::Bridge::FFmpeg::StreamInfo* audio = nullptr;
    for (const auto& stream : media.streams) {
        if (video == nullptr && stream.media_type == "video") {
            video = &stream;
        } else if (audio == nullptr && stream.media_type == "audio") {
            audio = &stream;
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
