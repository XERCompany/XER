#include "media/demuxer/stream_parser.h"

#include <cstring>

namespace XER::Media {

const char* StreamParser::Sniff(const std::uint8_t* data,
                                std::size_t size) {
    if (!data || size < 8) return "raw";
    // EBML magic (webm/mkv).
    if (data[0] == 0x1A && data[1] == 0x45 && data[2] == 0xDF &&
        data[3] == 0xA3)
        return "webm";
    // '....ftyp' (mp4/mov).
    if (std::memcmp(data + 4, "ftyp", 4) == 0) return "mp4";
    // RIFF/WAVE.
    if (std::memcmp(data, "RIFF", 4) == 0) return "wav";
    // OggS.
    if (std::memcmp(data, "OggS", 4) == 0) return "ogg";
    return "raw";
}

}  // namespace XER::Media
