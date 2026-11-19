#include "media/gpu/av1_decoder.h"

#include <utility>

namespace XER::Media {

CodecId AV1Decoder::accelerated_codec() const {
    return CodecId::kAV1;
}

bool AV1Decoder::HardwareAvailable() const {
    // Probed at registration time via the accelerator factory; on this
    // build the D3D11VA/DXVA path reports unavailable until implemented.
    return false;
}

bool AV1Decoder::Initialize(const DemuxerStream& stream,
                                 std::string* error_out) {
    if (stream.type() != StreamType::kVideo) {
        if (error_out) *error_out = "not a video stream";
        return false;
    }
    if (!HardwareAvailable()) {
        if (error_out) *error_out = "hardware AV1 decode unavailable";
        return false;
    }
    return true;
}

void AV1Decoder::Decode(const DemuxerStream::ReadResult& unit,
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

bool AV1Decoder::CanDecode(CodecId codec) const {
    return codec == CodecId::kAV1;
}

}  // namespace XER::Media
