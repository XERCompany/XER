#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#if defined(__has_include)
#if __has_include(<FLAC/stream_decoder.h>)
#include <FLAC/stream_decoder.h>
#define ENGINE_CODEC_HAS_FLAC_HEADERS 1
#else
#define ENGINE_CODEC_HAS_FLAC_HEADERS 0
#endif
#else
#define ENGINE_CODEC_HAS_FLAC_HEADERS 0
#endif

namespace XER::Audio::CodecIO {

class FlacDecoder {
public:
    bool Decode(const uint8_t* data, size_t bytes, std::vector<float>& out) const;
};

}  // namespace XER::Audio::CodecIO
