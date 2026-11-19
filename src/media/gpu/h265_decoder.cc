#include "media/gpu/h265_decoder.h"

#include <utility>

namespace XER::Media {

CodecId H265Decoder::accelerated_codec() const {
    return CodecId::kH265;
}

bool H265Decoder::HardwareAvailable() const {
    // Probed at registration time via the accelerator factory; on this
    // build the D3D11VA/DXVA path reports unavailable until implemented.
    return false;
}

bool H265Decoder::Initialize(const DemuxerStream& stream,
                                 std::string* error_out) {
    if (stream.type() != StreamType::kVideo) {
        if (error_out) *error_out = "not a video stream";
        return false;
    }
    if (!HardwareAvailable()) {
        if (error_out) *error_out = "hardware H265 decode unavailable";
        return false;
    }
    return true;
}

void H265Decoder::Decode(const DemuxerStream::ReadResult& unit,
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

bool H265Decoder::CanDecode(CodecId codec) const {
    return codec == CodecId::kH265;
}

}  // namespace XER::Media
