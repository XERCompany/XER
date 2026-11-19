/**
 * VideoDecoderSelector — picks the video decoder (design doc: hardware
 * via GpuVideoAcceleratorFactory first, registered software decoders as
 * fallback).
 */
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "media/decoders/video_decoder.h"

namespace XER::Media {

class VideoDecoderSelector {
public:
    explicit VideoDecoderSelector(bool prefer_hardware = true)
        : prefer_hardware_(prefer_hardware) {}

    void AddCandidate(
        std::function<std::unique_ptr<VideoDecoder>()> factory);
    void set_prefer_hardware(bool v) { prefer_hardware_ = v; }

    std::unique_ptr<VideoDecoder> Select(const DemuxerStream& stream,
                                         std::string* error_out);

private:
    bool prefer_hardware_;
    std::vector<std::function<std::unique_ptr<VideoDecoder>()>>
        candidates_;
};

}  // namespace XER::Media
