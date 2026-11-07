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

    XER::engine::bridge::ffmpeg::MediaInfo media;
    if (!XER::engine::bridge::ffmpeg::ProbeMedia(path, &media, error_out)) {
        return false;
    }

    const XER::engine::bridge::ffmpeg::StreamInfo* video = nullptr;
    const XER::engine::bridge::ffmpeg::StreamInfo* audio = nullptr;
    for (const auto& stream : media.streams) {
        if (video == nullptr && stream.media_type == "video") {
            video = &stream;
        } else if (audio == nullptr && stream.media_type == "audio") {
            audio = &stream;
        }
    }
    if (video == nullptr) {
        if (error_out != nullptr) *error_out = "no video stream";
        return false;
    }

    info->width = video->width;
    info->height = video->height;
    info->video_codec = video->codec_name;
    info->has_audio = audio != nullptr;
    info->audio_codec = audio != nullptr ? audio->codec_name : "";

    // Container duration is in AV_TIME_BASE units (microseconds).
    if (media.duration > 0) {
        info->duration_seconds =
            static_cast<double>(media.duration) / 1000000.0;
    } else if (video->duration > 0 && video->time_base_den > 0) {
        info->duration_seconds = static_cast<double>(video->duration) *
                                 video->time_base_num /
                                 static_cast<double>(video->time_base_den);
    }
    // Frame-rate estimate from the stream header; the decoder reports the
    // authoritative value once opened.
    if (video->frame_count > 0 && info->duration_seconds > 0.0) {
        info->fps = video->frame_count / info->duration_seconds;
    }

    std::error_code ec;
    const auto size = std::filesystem::file_size(
        std::filesystem::path(XER::Engine::Helper::String::Utf8ToWide(path)), ec);
    if (!ec) {
        info->file_size_bytes = static_cast<std::int64_t>(size);
    }

    info->valid = true;
    return true;
}

}  // namespace XER::Content::Video
