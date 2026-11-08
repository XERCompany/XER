#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "async_io/log_and_debug/io_dump_helper.h"

namespace XER::Audio::CodecIO {

struct Id3Tag {
    std::string title;
    std::string artist;
    std::string album;
};

class MetadataId3 {
public:
    bool Parse(const uint8_t* data, size_t bytes, Id3Tag& tag) const;
    std::vector<uint8_t> Build(const Id3Tag& tag) const;
};

}  // namespace XER::Audio::CodecIO
