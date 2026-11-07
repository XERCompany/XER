#include "codec_wav_float.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace XER::Engine::Audio::CodecIO {

namespace {

template <typename IntegerType>
void WriteLittleEndian(std::vector<uint8_t>* out, IntegerType value) {
    for (size_t index = 0; index < sizeof(IntegerType); ++index) {
        out->push_back(static_cast<uint8_t>((static_cast<std::uint64_t>(value) >> (index * 8u)) & 0xFFu));
    }
}

template <typename IntegerType>
IntegerType ReadLittleEndian(const uint8_t* data) {
    IntegerType value = 0;
    for (size_t index = 0; index < sizeof(IntegerType); ++index) {
        value |= static_cast<IntegerType>(static_cast<std::uint64_t>(data[index]) << (index * 8u));
    }
    return value;
}

}  // namespace

bool WavFloatCodec::Encode32(const float* input, size_t frames, std::vector<uint8_t>& out) const {
    if (input == nullptr || frames == 0) {
        return false;
    }

    constexpr std::uint16_t channels = 1;
    constexpr std::uint32_t sample_rate = 44100;
    constexpr std::uint16_t bits_per_sample = 32;
    constexpr std::uint32_t audio_format = 3;
    const std::uint32_t data_size = static_cast<std::uint32_t>(frames * sizeof(float));
    const std::uint32_t riff_size = 4u + 8u + 16u + 8u + data_size;

    out.clear();
    out.reserve(44u + data_size);
    out.insert(out.end(), {'R', 'I', 'F', 'F'});
    WriteLittleEndian<std::uint32_t>(&out, riff_size);
    out.insert(out.end(), {'W', 'A', 'V', 'E'});
    out.insert(out.end(), {'f', 'm', 't', ' '});
    WriteLittleEndian<std::uint32_t>(&out, 16u);
    WriteLittleEndian<std::uint16_t>(&out, static_cast<std::uint16_t>(audio_format));
    WriteLittleEndian<std::uint16_t>(&out, channels);
    WriteLittleEndian<std::uint32_t>(&out, sample_rate);
    WriteLittleEndian<std::uint32_t>(&out, sample_rate * channels * (bits_per_sample / 8u));
    WriteLittleEndian<std::uint16_t>(&out, static_cast<std::uint16_t>(channels * (bits_per_sample / 8u)));
    WriteLittleEndian<std::uint16_t>(&out, bits_per_sample);
    out.insert(out.end(), {'d', 'a', 't', 'a'});
    WriteLittleEndian<std::uint32_t>(&out, data_size);

    const size_t payload_offset = out.size();
    out.resize(payload_offset + data_size);
    std::memcpy(out.data() + static_cast<std::ptrdiff_t>(payload_offset), input, data_size);
    return true;
}

bool WavFloatCodec::Decode32(const uint8_t* data, size_t bytes, std::vector<float>& out) const {
    if (data == nullptr || bytes < 44u) {
        return false;
    }
    if (std::memcmp(data, "RIFF", 4) != 0 || std::memcmp(data + 8, "WAVE", 4) != 0) {
        return false;
    }

    size_t cursor = 12u;
    uint16_t audio_format = 0;
    uint16_t channels = 0;
    uint16_t bits_per_sample = 0;
    const uint8_t* data_chunk = nullptr;
    uint32_t data_chunk_size = 0;
    while (cursor + 8u <= bytes) {
        const uint8_t* chunk = data + static_cast<std::ptrdiff_t>(cursor);
        const uint32_t chunk_size = ReadLittleEndian<std::uint32_t>(chunk + 4);
        cursor += 8u;
        if (cursor + chunk_size > bytes) {
            return false;
        }
        if (std::memcmp(chunk, "fmt ", 4) == 0) {
            if (chunk_size < 16u) {
                return false;
            }
            audio_format = ReadLittleEndian<std::uint16_t>(data + static_cast<std::ptrdiff_t>(cursor));
            channels = ReadLittleEndian<std::uint16_t>(data + static_cast<std::ptrdiff_t>(cursor + 2u));
            bits_per_sample = ReadLittleEndian<std::uint16_t>(data + static_cast<std::ptrdiff_t>(cursor + 14u));
        } else if (std::memcmp(chunk, "data", 4) == 0) {
            data_chunk = data + static_cast<std::ptrdiff_t>(cursor);
            data_chunk_size = chunk_size;
        }
        cursor += chunk_size + (chunk_size % 2u);
    }

    if (audio_format != 3u || channels != 1u || bits_per_sample != 32u || data_chunk == nullptr || (data_chunk_size % sizeof(float)) != 0u) {
        return false;
    }

    out.resize(data_chunk_size / sizeof(float));
    std::memcpy(out.data(), data_chunk, data_chunk_size);
    return !out.empty();
}

}  // namespace XER::Engine::Audio::CodecIO
