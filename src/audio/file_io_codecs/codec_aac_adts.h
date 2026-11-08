#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "async_io/log_and_debug/io_dump_helper.h"

namespace XER::Audio::CodecIO {

class AacAdtsCodec {
public:
    bool Encode(const float* input, size_t frames, std::vector<uint8_t>& out) const;
    bool Decode(const uint8_t* data, size_t bytes, std::vector<float>& out) const;
};

}  // namespace XER::Audio::CodecIO
