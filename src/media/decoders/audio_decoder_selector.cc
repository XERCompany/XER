#include "media/decoders/audio_decoder_selector.h"

namespace XER::Media {

void AudioDecoderSelector::AddCandidate(
    std::function<std::unique_ptr<AudioDecoder>()> factory) {
    candidates_.push_back(std::move(factory));
}

std::unique_ptr<AudioDecoder> AudioDecoderSelector::Select(
    const DemuxerStream& stream, std::string* error_out) {
    for (auto& f : candidates_) {
        auto dec = f();
        if (dec && dec->CanDecode(stream.codec()))
            return dec;
    }
    if (error_out)
        *error_out =
            std::string("no audio decoder for ") + CodecName(stream.codec());
    return nullptr;
}

}  // namespace XER::Media
