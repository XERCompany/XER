#include "media/decoders/video_decoder_selector.h"

#include "media/gpu/gpu_media.h"

namespace XER::Media {

void VideoDecoderSelector::AddCandidate(
    std::function<std::unique_ptr<VideoDecoder>()> factory) {
    candidates_.push_back(std::move(factory));
}

std::unique_ptr<VideoDecoder> VideoDecoderSelector::Select(
    const DemuxerStream& stream, std::string* error_out) {
    if (prefer_hardware_) {
        if (auto* accel = GpuVideoAcceleratorFactory::Instance()
                              .AcceleratorFor(stream.codec())) {
            if (auto dec = accel->CreateDecoder()) return dec;
        }
    }
    for (auto& f : candidates_) {
        auto dec = f();
        if (dec && dec->CanDecode(stream.codec())) return dec;
    }
    if (error_out)
        *error_out =
            std::string("no video decoder for ") + CodecName(stream.codec());
    return nullptr;
}

}  // namespace XER::Media
