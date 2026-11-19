/**
 * AudioDecoderSelector — picks the audio decoder for a stream (design
 * doc: tries each registered decoder in order — platform/hardware
 * first, FFmpeg software last — and binds the first whose CanDecode()
 * accepts).
 */
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "media/decoders/audio_decoder.h"

namespace XER::Media {

class AudioDecoderSelector {
public:
    // Registers a candidate factory; earlier = preferred.
    void AddCandidate(
        std::function<std::unique_ptr<AudioDecoder>()> factory);

    // Returns the first decoder accepting the stream's codec, already
    // Initialize()d, or nullptr with error_out.
    std::unique_ptr<AudioDecoder> Select(const DemuxerStream& stream,
                                         std::string* error_out);

private:
    std::vector<std::function<std::unique_ptr<AudioDecoder>()>>
        candidates_;
};

}  // namespace XER::Media
