#include "media/gpu/vp9_decoder.h"

#include <utility>

namespace XER::Media {

CodecId VP9Decoder::accelerated_codec() const {
    return CodecId::kVP9;
}

bool VP9Decoder::HardwareAvailable() const {
    // Probed at registration time via the accelerator factory; on this
    // build the D3D11VA/DXVA path reports unavailable until implemented.
    return false;
}

bool VP9Decoder::Initialize(const DemuxerStream& stream,
                                 std::string* error_out) {
    if (stream.type() != StreamType::kVideo) {
        if (error_out) *error_out = "not a video stream";
        return false;
    }
    if (!HardwareAvailable()) {
        if (error_out) *error_out = "hardware VP9 decode unavailable";
        return false;
    }
    return true;
}

void VP9Decoder::Decode(const DemuxerStream::ReadResult& unit,
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

bool VP9Decoder::CanDecode(CodecId codec) const {
    return codec == CodecId::kVP9;
}

}  // namespace XER::Media
