#include "helper/compression_codec.h"

#include <zlib.h>

namespace XER::Engine::Common {

std::optional<std::vector<std::uint8_t>> CompressionCodec::Compress(const std::string& text) {
    if (text.empty()) {
        return std::vector<std::uint8_t>{};
    }

    const uLong source_len = static_cast<uLong>(text.size());
    const uLongf bound = compressBound(source_len);
    if (bound == 0) {
        return std::nullopt;
    }

    std::vector<std::uint8_t> compressed(bound);
    uLongf output_len = bound;

    const int result = compress2(
        reinterpret_cast<Bytef*>(compressed.data()),
        &output_len,
        reinterpret_cast<const Bytef*>(text.data()),
        source_len,
        Z_BEST_SPEED);

    if (result != Z_OK) {
        return std::nullopt;
    }

    compressed.resize(output_len);
    return compressed;
}

std::optional<std::string> CompressionCodec::DecompressToString(const std::vector<std::uint8_t>& compressed) {
    if (compressed.empty()) {
        return std::string{};
    }

    z_stream stream{};
    stream.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(compressed.data()));
    stream.avail_in = static_cast<uInt>(compressed.size());
    if (inflateInit(&stream) != Z_OK) {
        return std::nullopt;
    }

    std::string output;
    std::vector<char> buffer(4096);

    int inflate_result = Z_OK;
    while (inflate_result == Z_OK) {
        stream.next_out = reinterpret_cast<Bytef*>(buffer.data());
        stream.avail_out = static_cast<uInt>(buffer.size());

        inflate_result = inflate(&stream, Z_NO_FLUSH);
        const std::size_t produced = buffer.size() - stream.avail_out;
        output.append(buffer.data(), produced);
    }

    inflateEnd(&stream);
    if (inflate_result != Z_STREAM_END) {
        return std::nullopt;
    }

    return output;
}

}  // namespace XER::engine::javascript::common
