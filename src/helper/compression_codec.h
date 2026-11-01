
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Engine::Common {

class CompressionCodec {
public:
    static std::optional<std::vector<std::uint8_t>> Compress(const std::string& text);
    static std::optional<std::string> DecompressToString(const std::vector<std::uint8_t>& compressed);
};

}  // namespace Engine::Common
