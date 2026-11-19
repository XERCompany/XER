/**
 * FFmpegMuxer — Muxer implementation backed by the FFmpeg bridge encode
 * API (EncodeVideoFrameStream / EncodeAudioVideoStream). Packets are
 * buffered through MuxerTimestampAdapter and flushed on Close() via the
 * bridge providers.
 *
 * Video packets are expected to carry raw frames (BGRA8) in .data —
 * muxers operate on decoded frames, encoding is the muxer's job per the
 * doc (recording path, not packet copy).
 */
#pragma once

#include "media/muxers/muxer.h"
#include "media/muxers/muxer_timestamp_adapter.h"

namespace XER::Media {

class FFmpegMuxer final : public Muxer {
public:
    explicit FFmpegMuxer(Container container);

    bool Open(Container container, const std::string& path,
            std::string* error_out) override;
    bool AddStream(StreamType type, CodecId codec,
                   std::string* error_out) override;
    bool WritePacket(const MuxedPacket& packet,
                     std::string* error_out) override;
    bool Close(std::string* error_out) override;

private:
    Container container_;
    std::string path_;
    bool open_ = false;
    bool has_video_ = false;
    bool has_audio_ = false;
    CodecId video_codec_ = CodecId::kUnknown;
    CodecId audio_codec_ = CodecId::kUnknown;
    MuxerTimestampAdapter timestamps_;
    std::vector<MuxedPacket> video_packets_;
    std::vector<MuxedPacket> audio_packets_;
};

}  // namespace XER::Media
