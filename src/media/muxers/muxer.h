/**
 * XER::Media muxers (design doc). Muxers are used by MediaRecorder-style
 * recording; see container_codecs.h for the supported codec table and
 * muxer_timestamp_adapter.h for clock alignment.
 */
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "media/base/media_types.h"
#include "media/muxers/container_codecs.h"
#include "media/muxers/muxer_timestamp_adapter.h"

namespace XER::Media {

struct MuxedPacket {
    StreamType type = StreamType::kUnknown;
    CodecId codec = CodecId::kUnknown;
    TimeTicks timestamp = 0;
    TimeTicks duration = 0;
    std::vector<std::uint8_t> data;
};

class Muxer {
public:
    virtual ~Muxer() = default;
    virtual bool Open(Container container, const std::string& path,
                      std::string* error_out) = 0;
    virtual bool AddStream(StreamType type, CodecId codec,
                           std::string* error_out) = 0;
    virtual bool WritePacket(const MuxedPacket& packet,
                             std::string* error_out) = 0;
    virtual bool Close(std::string* error_out) = 0;
};

// Factory: concrete muxers live in webm_muxer.h / mkv_muxer.h / mp4_muxer.h.
std::unique_ptr<Muxer> CreateMuxer(Container container);

}  // namespace XER::Media
