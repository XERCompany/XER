/**
 * H265Decoder — hardware-accelerated H265 video decoder
 * (XER::Media::AcceleratedVideoDecoder implementation; registers with
 * GpuVideoAcceleratorFactory; falls back to software when the platform
 * accelerator is unavailable).
 */
#pragma once

#include "media/gpu/gpu_media.h"

namespace XER::Media {

class H265Decoder final : public AcceleratedVideoDecoder {
public:
    CodecId accelerated_codec() const override;
    bool HardwareAvailable() const override;
    bool Initialize(const DemuxerStream& stream,
                    std::string* error_out) override;
    void Decode(const DemuxerStream::ReadResult& unit,
                DecodeCallback cb) override;
    void Reset() override {}
    bool CanDecode(CodecId codec) const override;
};

}  // namespace XER::Media
