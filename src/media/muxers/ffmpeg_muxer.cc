#include "media/muxers/ffmpeg_muxer.h"

#include "wrapper/ffmpeg/ffmpeg_engine_bridge.h"

namespace XER::Media {
namespace {
std::string CodecNameFor(CodecId c, StreamType t) {
    // Bridge codec_name strings follow FFmpeg encoder names.
    if (t == StreamType::kAudio) {
        return c == CodecId::kAAC ? "aac" : "libopus";
    }
    switch (c) {
        case CodecId::kH264: return "libx264";
        case CodecId::kH265: return "libx265";
        case CodecId::kVP8:  return "libvpx";
        case CodecId::kVP9:  return "libvpx-vp9";
        case CodecId::kAV1:  return "libaom-av1";
        default:             return "libx264";
    }
}
}  // namespace

FFmpegMuxer::FFmpegMuxer(Container container) : container_(container) {}

bool FFmpegMuxer::Open(Container container, const std::string& path,
                       std::string* error_out) {
    if (container != container_) {
        if (error_out) *error_out = "container mismatch";
        return false;
    }
    if (path.empty()) {
        if (error_out) *error_out = "empty output path";
        return false;
    }
    path_ = path;
    open_ = true;
    return true;
}

bool FFmpegMuxer::AddStream(StreamType type, CodecId codec,
                            std::string* error_out) {
    if (!open_) {
        if (error_out) *error_out = "muxer not open";
        return false;
    }
    if (!ContainerSupportsCodec(container_, codec)) {
        if (error_out) {
            *error_out = std::string("codec ") + CodecName(codec) +
                         " not supported in " + ContainerName(container_);
        }
        return false;
    }
    if (type == StreamType::kVideo) {
        has_video_ = true;
        video_codec_ = codec;
    } else if (type == StreamType::kAudio) {
        has_audio_ = true;
        audio_codec_ = codec;
    }
    return true;
}

bool FFmpegMuxer::WritePacket(const MuxedPacket& packet,
                              std::string* error_out) {
    if (!open_) {
        if (error_out) *error_out = "muxer not open";
        return false;
    }
    MuxedPacket p = packet;
    p.timestamp = timestamps_.Normalize(p.type, p.timestamp);
    if (p.type == StreamType::kVideo) video_packets_.push_back(std::move(p));
    else if (p.type == StreamType::kAudio) audio_packets_.push_back(std::move(p));
    return true;
}

bool FFmpegMuxer::Close(std::string* error_out) {
    if (!open_) return true;
    open_ = false;
    if (video_packets_.empty() && audio_packets_.empty()) return true;
    if (video_packets_.empty()) {
        if (error_out) *error_out = "audio-only mux not implemented yet";
        return false;
    }
    // Feed buffered frames through the bridge encode providers.
    namespace BF = XER::Bridge::FFmpeg;
    BF::EncodeVideoParams vp;
    vp.output_path = path_;
    vp.codec_name = CodecNameFor(video_codec_, StreamType::kVideo);
    vp.width = 0;   // inferred from first frame by the bridge
    vp.height = 0;
    const auto vcount = video_packets_.size();
    BF::VideoFrameProvider vprov =
        [this](std::size_t i, BF::VideoFrameInfo* out,
               std::string*) -> bool {
        if (i >= video_packets_.size()) return false;
        const auto& p = video_packets_[i];
        out->data = p.data;
        out->pts = p.timestamp;
        out->duration = p.duration;
        return true;
    };
    if (audio_packets_.empty())
        return BF::EncodeVideoFrameStream(vp, vcount, vprov, error_out);

    BF::EncodeAudioVideoParams av;
    av.video = vp;
    av.audio.output_path = path_;
    av.audio.codec_name = CodecNameFor(audio_codec_, StreamType::kAudio);
    const auto acount = audio_packets_.size();
    BF::AudioFrameStreamProvider aprov =
        [this](std::size_t i, BF::AudioFrameInfo* out,
               std::string*) -> bool {
        if (i >= audio_packets_.size()) return false;
        const auto& p = audio_packets_[i];
        out->data = p.data;
        out->pts = p.timestamp;
        return true;
    };
    return BF::EncodeAudioVideoStream(av, vcount, vprov, acount, aprov,
                                    error_out);
}

}  // namespace XER::Media
