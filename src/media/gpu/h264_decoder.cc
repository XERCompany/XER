#include "media/gpu/h264_decoder.h"

#include <utility>

namespace XER::Media {

CodecId H264Decoder::accelerated_codec() const {
    return CodecId::kH264;
}

bool H264Decoder::HardwareAvailable() const {
    // Probed at registration time via the accelerator factory; on this
    // build the D3D11VA/DXVA path reports unavailable until implemented.
    return false;
}

bool H264Decoder::Initialize(const DemuxerStream& stream,
                                 std::string* error_out) {
    if (stream.type() != StreamType::kVideo) {
        if (error_out) *error_out = "not a video stream";
        return false;
    }
    if (!HardwareAvailable()) {
        if (error_out) *error_out = "hardware H264 decode unavailable";
        return false;
    }
    return true;
}

void H264Decoder::Decode(const DemuxerStream::ReadResult& unit,
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

bool H264Decoder::CanDecode(CodecId codec) const {
    return codec == CodecId::kH264;
}

}  // namespace XER::Media
