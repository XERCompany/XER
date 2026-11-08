#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace XER::Audio::CodecIO::detail {

bool EncodeMonoAudioBufferWithFfmpeg(
	const float* input,
	size_t frames,
	int sample_rate,
	const std::string& extension,
	const std::string& codec_name,
	std::vector<uint8_t>* encoded_bytes,
	std::string* error_out = nullptr);

bool EncodeInterleavedAudioBufferWithFfmpeg(
	const float* input,
	size_t frames,
	int sample_rate,
	int channels,
	const std::string& extension,
	const std::string& codec_name,
	std::vector<uint8_t>* encoded_bytes,
	std::string* error_out = nullptr);

}  // namespace XER::Audio::CodecIO::detail