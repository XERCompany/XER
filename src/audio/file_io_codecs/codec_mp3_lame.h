#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#if defined(__has_include)
#if __has_include(<lame/lame.h>)
#include <lame/lame.h>
#define ENGINE_CODEC_HAS_LAME_HEADERS 1
#else
#define ENGINE_CODEC_HAS_LAME_HEADERS 0
#endif
#else
#define ENGINE_CODEC_HAS_LAME_HEADERS 0
#endif

namespace XER::Engine::Audio::CodecIO {

class Mp3LameCodec {
public:
    bool Encode(const float* input, size_t frames, std::vector<uint8_t>& out) const;
    bool EncodeInterleaved(const float* input, size_t frames, int sample_rate, int channels, std::vector<uint8_t>& out) const;
    bool Decode(const uint8_t* data, size_t bytes, std::vector<float>& out) const;
};

}  // namespace XER::Engine::Audio::CodecIO
