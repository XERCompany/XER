#include "codec_wav_pcm.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace XER::Audio::CodecIO {

namespace {

constexpr std::size_t kWavHeaderBytes = 44;

void WriteLe16(std::vector<uint8_t>& out, std::size_t offset, std::uint16_t value) {
	out[offset + 0] = static_cast<uint8_t>(value & 0xFFu);
	out[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFFu);
}

void WriteLe32(std::vector<uint8_t>& out, std::size_t offset, std::uint32_t value) {
	out[offset + 0] = static_cast<uint8_t>(value & 0xFFu);
	out[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFFu);
	out[offset + 2] = static_cast<uint8_t>((value >> 16) & 0xFFu);
	out[offset + 3] = static_cast<uint8_t>((value >> 24) & 0xFFu);
}

std::uint16_t ReadLe16(const uint8_t* ptr) {
	return static_cast<std::uint16_t>(ptr[0])
		| (static_cast<std::uint16_t>(ptr[1]) << 8);
}

std::uint32_t ReadLe32(const uint8_t* ptr) {
	return static_cast<std::uint32_t>(ptr[0])
		| (static_cast<std::uint32_t>(ptr[1]) << 8)
		| (static_cast<std::uint32_t>(ptr[2]) << 16)
		| (static_cast<std::uint32_t>(ptr[3]) << 24);
}

} // namespace

bool WavPcmCodec::Encode16(
	const float* input,
	size_t frames,
	std::vector<uint8_t>& out,
	int sample_rate,
	int channels) const {
	if (input == nullptr || frames == 0 || sample_rate <= 0 || channels <= 0) {
        return false;
    }

	const std::uint16_t wav_channels = static_cast<std::uint16_t>(channels);
    const std::uint32_t wav_sample_rate = static_cast<std::uint32_t>(sample_rate);
    const std::uint16_t bits_per_sample = 16;
	const std::uint16_t block_align = static_cast<std::uint16_t>(wav_channels * (bits_per_sample / 8));
    const std::uint32_t byte_rate = wav_sample_rate * block_align;
    const std::uint32_t data_size = static_cast<std::uint32_t>(frames * block_align);

    out.assign(kWavHeaderBytes + data_size, 0);
    std::memcpy(out.data() + 0, "RIFF", 4);
    WriteLe32(out, 4, 36u + data_size);
    std::memcpy(out.data() + 8, "WAVE", 4);
    std::memcpy(out.data() + 12, "fmt ", 4);
    WriteLe32(out, 16, 16u);
    WriteLe16(out, 20, 1u);
	WriteLe16(out, 22, wav_channels);
    WriteLe32(out, 24, wav_sample_rate);
    WriteLe32(out, 28, byte_rate);
    WriteLe16(out, 32, block_align);
    WriteLe16(out, 34, bits_per_sample);
    std::memcpy(out.data() + 36, "data", 4);
    WriteLe32(out, 40, data_size);

    for (size_t frame = 0; frame < frames; ++frame) {
        for (int channel = 0; channel < channels; ++channel) {
            const size_t sample_index = frame * static_cast<size_t>(channels) + static_cast<size_t>(channel);
            const float clamped = std::clamp(input[sample_index], -1.0f, 1.0f);
            const int16_t s = static_cast<int16_t>(clamped * 32767.0f);
            const std::size_t offset = kWavHeaderBytes + sample_index * 2u;
            out[offset + 0] = static_cast<uint8_t>(s & 0xFF);
            out[offset + 1] = static_cast<uint8_t>((s >> 8) & 0xFF);
        }
    }
    return true;
}

bool WavPcmCodec::Decode16(
    const uint8_t* data,
    size_t bytes,
    std::vector<float>& out,
    int* sample_rate_out,
    int* channels_out) const {
    if (data == nullptr || bytes < kWavHeaderBytes) {
        return false;
    }

    const auto has_tag = [data](std::size_t offset, const char (&tag)[5]) {
        return std::memcmp(data + offset, tag, 4) == 0;
    };
    if (!has_tag(0, "RIFF") || !has_tag(8, "WAVE")) {
        return false;
    }

    std::size_t cursor = 12;
    std::size_t data_offset = 0;
    std::size_t data_size = 0;
    std::uint16_t audio_format = 0;
    std::uint16_t channels = 0;
    std::uint16_t bits_per_sample = 0;
    std::uint32_t sample_rate = 0;
    while (cursor + 8 <= bytes) {
        const char* chunk_id = reinterpret_cast<const char*>(data + cursor);
        const std::uint32_t chunk_size = ReadLe32(data + cursor + 4);
        const std::size_t chunk_data = cursor + 8;
        if (chunk_data + chunk_size > bytes) {
            return false;
        }
        if (std::memcmp(chunk_id, "fmt ", 4) == 0 && chunk_size >= 16) {
            audio_format = ReadLe16(data + chunk_data + 0);
            channels = ReadLe16(data + chunk_data + 2);
            sample_rate = ReadLe32(data + chunk_data + 4);
            bits_per_sample = ReadLe16(data + chunk_data + 14);
        } else if (std::memcmp(chunk_id, "data", 4) == 0) {
            data_offset = chunk_data;
            data_size = chunk_size;
        }
        cursor = chunk_data + chunk_size + (chunk_size & 1u);
    }

    if (audio_format != 1 || channels == 0 || bits_per_sample != 16 || data_size < 2) {
        return false;
    }

    const std::size_t sample_bytes = static_cast<std::size_t>(bits_per_sample / 8);
    if ((data_size % (sample_bytes * channels)) != 0) {
        return false;
    }

    const std::size_t frames = data_size / (sample_bytes * channels);
    out.resize(frames * channels);
    for (std::size_t frame = 0; frame < frames; ++frame) {
        for (std::size_t ch = 0; ch < channels; ++ch) {
            const std::size_t sample_offset = data_offset + (frame * channels + ch) * sample_bytes;
            const int16_t s = static_cast<int16_t>(data[sample_offset + 0] | (data[sample_offset + 1] << 8));
            out[frame * channels + ch] = static_cast<float>(s) / 32768.0f;
        }
    }

    if (sample_rate_out != nullptr) {
        *sample_rate_out = static_cast<int>(sample_rate);
    }
    if (channels_out != nullptr) {
        *channels_out = static_cast<int>(channels);
    }

    return true;
}

}  // namespace XER::Audio::CodecIO
