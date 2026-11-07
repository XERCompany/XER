#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "async_io/log_and_debug/io_dump_helper.h"

namespace XER::Engine::Audio::CodecIO {

class WavPcmCodec {
public:
    bool Encode16(
        const float* input,
        size_t frames,
        std::vector<uint8_t>& out,
        int sample_rate = 44100,
        int channels = 1) const;
    bool Decode16(
        const uint8_t* data,
        size_t bytes,
        std::vector<float>& out,
        int* sample_rate_out = nullptr,
        int* channels_out = nullptr) const;
};

}  // namespace XER::Engine::Audio::CodecIO
