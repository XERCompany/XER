#include "codec_ffmpeg_encode_helper.h"

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include "wrapper/ffmpeg/ffmpeg_engine_bridge.h"

namespace XER::Engine::Audio::CodecIO::detail {

namespace {

std::filesystem::path CreateTempPath(const std::string& extension) {
	const auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
	std::string normalized_extension = extension;
	if (!normalized_extension.empty() && normalized_extension.front() != '.') {
		normalized_extension.insert(normalized_extension.begin(), '.');
	}
	return std::filesystem::temp_directory_path() / ("xer_codec_encode_" + std::to_string(now) + normalized_extension);
}

class TempFileGuard {
public:
	explicit TempFileGuard(std::filesystem::path path) : path_(std::move(path)) {}
	~TempFileGuard() {
		std::error_code error;
		std::filesystem::remove(path_, error);
	}
	const std::filesystem::path& path() const { return path_; }

private:
	std::filesystem::path path_;
};

bool ReadBinaryFile(const std::filesystem::path& path, std::vector<uint8_t>* bytes_out, std::string* error_out) {
	if (bytes_out == nullptr) {
		if (error_out != nullptr) {
			*error_out = "audio encode byte target is null";
		}
		return false;
	}
	std::ifstream input(path, std::ios::binary | std::ios::ate);
	if (!input.is_open()) {
		if (error_out != nullptr) {
			*error_out = "failed to open encoded audio output: " + path.string();
		}
		return false;
	}
	const std::streamsize size = input.tellg();
	if (size < 0) {
		if (error_out != nullptr) {
			*error_out = "failed to size encoded audio output: " + path.string();
		}
		return false;
	}
	bytes_out->assign(static_cast<size_t>(size), 0);
	input.seekg(0, std::ios::beg);
	if (size > 0) {
		input.read(reinterpret_cast<char*>(bytes_out->data()), size);
	}
	if (!input) {
		if (error_out != nullptr) {
			*error_out = "failed to read encoded audio output: " + path.string();
		}
		return false;
	}
	return true;
}

}  // namespace

bool EncodeMonoAudioBufferWithFfmpeg(
	const float* input,
	size_t frames,
	int sample_rate,
	const std::string& extension,
	const std::string& codec_name,
	std::vector<uint8_t>* encoded_bytes,
	std::string* error_out) {
	return EncodeInterleavedAudioBufferWithFfmpeg(
		input,
		frames,
		sample_rate,
		1,
		extension,
		codec_name,
		encoded_bytes,
		error_out);
}

bool EncodeInterleavedAudioBufferWithFfmpeg(
	const float* input,
	size_t frames,
	int sample_rate,
	int channels,
	const std::string& extension,
	const std::string& codec_name,
	std::vector<uint8_t>* encoded_bytes,
	std::string* error_out) {
	if (input == nullptr || frames == 0 || sample_rate <= 0 || channels <= 0 || encoded_bytes == nullptr) {
		if (error_out != nullptr) {
			*error_out = "invalid FFmpeg encode helper request";
		}
		return false;
	}
	if (!XER::engine::bridge::ffmpeg::IsAvailable()) {
		if (error_out != nullptr) {
			*error_out = "ffmpeg bridge is unavailable";
		}
		return false;
	}

	XER::engine::bridge::ffmpeg::AudioFrameInfo frame;
	frame.stream_index = 0;
	frame.sample_rate = sample_rate;
	frame.channels = channels;
	frame.sample_count = static_cast<int>(frames);
	frame.planar = false;
	frame.sample_format = "flt";
	frame.data.resize(frames * static_cast<size_t>(channels) * sizeof(float));
	std::memcpy(frame.data.data(), input, frame.data.size());

	const TempFileGuard temp_path(CreateTempPath(extension));
	XER::engine::bridge::ffmpeg::EncodeAudioParams params;
	params.output_path = temp_path.path().string();
	params.codec_name = codec_name;
	params.sample_rate = sample_rate;
	params.channels = channels;
	params.sample_format = "fltp";
	params.bit_rate = codec_name == "aac" ? 192000 : 192000;

	std::string encode_error;
	if (!XER::engine::bridge::ffmpeg::EncodeAudioFrames(params, std::vector<XER::engine::bridge::ffmpeg::AudioFrameInfo>{std::move(frame)}, &encode_error)) {
		if (error_out != nullptr) {
			*error_out = !encode_error.empty() ? encode_error : "ffmpeg audio encoding failed";
		}
		return false;
	}
	return ReadBinaryFile(temp_path.path(), encoded_bytes, error_out);
}

}  // namespace XER::Engine::Audio::CodecIO::detail