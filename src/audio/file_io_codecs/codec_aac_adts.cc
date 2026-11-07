#include "codec_aac_adts.h"

#include "codec_ffmpeg_decode_helper.h"
#include "codec_ffmpeg_encode_helper.h"

#include <cstdint>

namespace XER::Engine::Audio::CodecIO {

bool AacAdtsCodec::Encode(const float* input, size_t frames, std::vector<uint8_t>& out) const {
    if (input == nullptr || frames == 0) {
        return false;
    }
    std::string error;
    if (!detail::EncodeMonoAudioBufferWithFfmpeg(input, frames, 44100, ".aac", "aac", &out, &error)) {
        out.clear();
        return false;
    }
    return true;
}

bool AacAdtsCodec::Decode(const uint8_t* data, size_t bytes, std::vector<float>& out) const {
    if (data == nullptr || bytes == 0) {
        return false;
    }

	std::string error;
	if (detail::DecodeAudioBufferWithFfmpeg(data, bytes, ".aac", &out, &error)) {
		return true;
	}

    std::vector<float> decoded;
    std::size_t cursor = 0;
    while (cursor + 7 <= bytes) {
        if (data[cursor] != 0xFF || (data[cursor + 1] & 0xF0u) != 0xF0u) {
            return false;
        }
        const std::uint16_t frame_length = static_cast<std::uint16_t>(((data[cursor + 3] & 0x03u) << 11)
            | (data[cursor + 4] << 3)
            | ((data[cursor + 5] >> 5) & 0x07u));
        if (frame_length < 7 || cursor + frame_length > bytes) {
            return false;
        }

        const std::size_t payload_bytes = frame_length - 7;
        if ((payload_bytes % 2) != 0) {
            return false;
        }
        const std::size_t samples = payload_bytes / 2;
        const std::size_t payload_offset = cursor + 7;
        decoded.reserve(decoded.size() + samples);
        for (std::size_t i = 0; i < samples; ++i) {
            const std::int16_t s = static_cast<std::int16_t>(data[payload_offset + i * 2 + 0] | (data[payload_offset + i * 2 + 1] << 8));
            decoded.push_back(static_cast<float>(s) / 32768.0f);
        }
        cursor += frame_length;
    }
    if (decoded.empty()) {
        return false;
    }
    out = std::move(decoded);
    return true;
}

}  // namespace XER::Engine::Audio::CodecIO
