#include "media/gpu/vp8_decoder.h"

#include <utility>

namespace XER::Media {

CodecId VP8Decoder::accelerated_codec() const {
    return CodecId::kVP8;
}

bool VP8Decoder::HardwareAvailable() const {
    // Probed at registration time via the accelerator factory; on this
    // build the D3D11VA/DXVA path reports unavailable until implemented.
    return false;
}

bool VP8Decoder::Initialize(const DemuxerStream& stream,
                                 std::string* error_out) {
    if (stream.type() != StreamType::kVideo) {
        if (error_out) *error_out = "not a video stream";
        return false;
    }
    if (!HardwareAvailable()) {
        if (error_out) *error_out = "hardware VP8 decode unavailable";
        return false;
    }
    return true;
}

void VP8Decoder::Decode(const DemuxerStream::ReadResult& unit,
                             DecodeCallback cb) {
    // Passthrough until the DXVA backend lands — decoded units arrive
    // from the demuxer already decoded on this platform.
    DecodedFrame f;
    f.timestamp = unit.timestamp;
    f.duration = unit.duration;
    f.pixels = unit.data;
    cb(unit.ok ? PipelineStatus::kOk : PipelineStatus::kError,
       std::move(f));
}

bool VP8Decoder::CanDecode(CodecId codec) const {
    return codec == CodecId::kVP8;
}

}  // namespace XER::Media
