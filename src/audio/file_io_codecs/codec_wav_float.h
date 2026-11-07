#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "async_io/log_and_debug/io_perf_counter.h"

namespace XER::Engine::Audio::CodecIO {

class WavFloatCodec {
public:
    bool Encode32(const float* input, size_t frames, std::vector<uint8_t>& out) const;
    bool Decode32(const uint8_t* data, size_t bytes, std::vector<float>& out) const;
};

}  // namespace XER::Engine::Audio::CodecIO
