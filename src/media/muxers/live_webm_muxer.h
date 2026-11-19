/**
 * LiveWebmMuxer — streaming EBML/WebM writer (design doc: MediaRecorder
 * emits a live webm byte stream — EBML header, Segment info, Tracks,
 * then SimpleBlock/Cluster elements as packets arrive). Unlike
 * FFmpegMuxer this produces real container bytes incrementally.
 */
#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include "media/muxers/muxer.h"

namespace XER::Media {

class LiveWebmMuxer final : public Muxer {
public:
    // Bytes callback for streaming output (e.g. to a sink or file).
    using OutputCallback = std::function<void(const std::uint8_t* data,
                                              std::size_t size)>;
    void SetOutput(OutputCallback cb) { out_ = std::move(cb); }

    bool Open(Container container, const std::string& path,
            std::string* error_out) override;
    bool AddStream(StreamType type, CodecId codec,
                   std::string* error_out) override;
    bool WritePacket(const MuxedPacket& packet,
                     std::string* error_out) override;
    bool Close(std::string* error_out) override;

    // Exposed for tests: builds the EBML header + segment/track preamble.
    static std::vector<std::uint8_t> BuildHeader(bool has_video,
                                                 CodecId video_codec,
                                                 bool has_audio);

private:
    void Emit(const std::vector<std::uint8_t>& bytes);

    OutputCallback out_;
    bool open_ = false;
    bool header_sent_ = false;
    bool has_video_ = false;
    bool has_audio_ = false;
    CodecId video_codec_ = CodecId::kUnknown;
    MuxerTimestampAdapter timestamps_;
    std::vector<std::uint8_t> buffered_;  // when no callback: accumulate
};

}  // namespace XER::Media
