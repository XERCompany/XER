#include "codec_ffmpeg_decode_helper.h"
#include "helper/string_utils.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include "wrapper/ffmpeg/ffmpeg_engine_bridge.h"

namespace XER::Audio::CodecIO::detail {

namespace {

struct DecodedAudioData {
	std::vector<std::vector<float>> channels;
	int sample_rate = 0;

	size_t FrameCount() const {
		return channels.empty() ? 0 : channels.front().size();
	}
};

std::string CanonicalSampleFormat(std::string sample_format) {
	sample_format = XER::Helper::String::ToLowerAscii(std::move(sample_format));
	if (!sample_format.empty() && sample_format.back() == 'p') {
		sample_format.pop_back();
	}
	return sample_format;
}

size_t BytesPerSample(const std::string& sample_format) {
	const std::string canonical = CanonicalSampleFormat(sample_format);
	if (canonical == "u8") {
		return 1;
	}
	if (canonical == "s16") {
		return 2;
	}
	if (canonical == "s32" || canonical == "flt") {
		return 4;
	}
	if (canonical == "s64" || canonical == "dbl") {
		return 8;
	}
	return 0;
}

template <typename SampleType>
SampleType ReadPod(const std::uint8_t* data) {
	SampleType value{};
	std::memcpy(&value, data, sizeof(SampleType));
	return value;
}

bool DecodeSample(const std::string& sample_format, const std::uint8_t* data, float* sample_out) {
	if (data == nullptr || sample_out == nullptr) {
		return false;
	}

	const std::string canonical = CanonicalSampleFormat(sample_format);
	if (canonical == "u8") {
		*sample_out = (static_cast<float>(*data) - 128.0f) / 128.0f;
		return true;
	}
	if (canonical == "s16") {
		*sample_out = static_cast<float>(ReadPod<std::int16_t>(data)) / 32768.0f;
		return true;
	}
	if (canonical == "s32") {
		*sample_out = static_cast<float>(static_cast<double>(ReadPod<std::int32_t>(data)) / 2147483648.0);
		return true;
	}
	if (canonical == "s64") {
		*sample_out = static_cast<float>(static_cast<long double>(ReadPod<std::int64_t>(data)) / 9223372036854775808.0L);
		return true;
	}
	if (canonical == "flt") {
		*sample_out = ReadPod<float>(data);
		return true;
	}
	if (canonical == "dbl") {
		*sample_out = static_cast<float>(ReadPod<double>(data));
		return true;
	}
	return false;
}

bool AppendFrameChannels(
	const XER::Bridge::FFmpeg::AudioFrameInfo& frame,
	DecodedAudioData* decoded_audio,
	std::string* error_out) {
	if (decoded_audio == nullptr || frame.channels <= 0 || frame.sample_count <= 0) {
		if (error_out != nullptr) {
			*error_out = "invalid decoded audio frame";
		}
		return false;
	}

	if (decoded_audio->channels.empty()) {
		decoded_audio->channels.resize(static_cast<size_t>(frame.channels));
	} else if (decoded_audio->channels.size() != static_cast<size_t>(frame.channels)) {
		if (error_out != nullptr) {
			*error_out = "decoded audio frame changed channel count";
		}
		return false;
	}

	const size_t bytes_per_sample = BytesPerSample(frame.sample_format);
	if (bytes_per_sample == 0) {
		if (error_out != nullptr) {
			*error_out = "unsupported ffmpeg sample format: " + frame.sample_format;
		}
		return false;
	}

	for (auto& channel : decoded_audio->channels) {
		channel.reserve(channel.size() + static_cast<size_t>(frame.sample_count));
	}

	if (frame.planar) {
		if (frame.plane_sizes.size() < static_cast<size_t>(frame.channels)) {
			if (error_out != nullptr) {
				*error_out = "planar audio frame is missing channel planes";
			}
			return false;
		}

		std::vector<size_t> plane_offsets(static_cast<size_t>(frame.channels), 0);
		size_t cursor = 0;
		for (int channel = 0; channel < frame.channels; ++channel) {
			const size_t plane_size = static_cast<size_t>(frame.plane_sizes[static_cast<size_t>(channel)]);
			const size_t minimum_plane_size = static_cast<size_t>(frame.sample_count) * bytes_per_sample;
			if (plane_size < minimum_plane_size || cursor + plane_size > frame.data.size()) {
				if (error_out != nullptr) {
					*error_out = "planar audio frame payload is truncated";
				}
				return false;
			}
			plane_offsets[static_cast<size_t>(channel)] = cursor;
			cursor += plane_size;
		}

		for (int channel = 0; channel < frame.channels; ++channel) {
			for (int sample_index = 0; sample_index < frame.sample_count; ++sample_index) {
				float decoded_sample = 0.0f;
				const size_t byte_offset = plane_offsets[static_cast<size_t>(channel)]
					+ static_cast<size_t>(sample_index) * bytes_per_sample;
				if (!DecodeSample(frame.sample_format, frame.data.data() + static_cast<std::ptrdiff_t>(byte_offset), &decoded_sample)) {
					if (error_out != nullptr) {
						*error_out = "failed to decode planar audio sample";
					}
					return false;
				}
				decoded_audio->channels[static_cast<size_t>(channel)].push_back(decoded_sample);
			}
		}
		return true;
	}

	const size_t required_bytes = static_cast<size_t>(frame.sample_count)
		* static_cast<size_t>(frame.channels)
		* bytes_per_sample;
	if (frame.data.size() < required_bytes) {
		if (error_out != nullptr) {
			*error_out = "interleaved audio frame payload is truncated";
		}
		return false;
	}

	for (int sample_index = 0; sample_index < frame.sample_count; ++sample_index) {
		for (int channel = 0; channel < frame.channels; ++channel) {
			float decoded_sample = 0.0f;
			const size_t byte_offset = (static_cast<size_t>(sample_index) * static_cast<size_t>(frame.channels)
				+ static_cast<size_t>(channel)) * bytes_per_sample;
			if (!DecodeSample(frame.sample_format, frame.data.data() + static_cast<std::ptrdiff_t>(byte_offset), &decoded_sample)) {
				if (error_out != nullptr) {
					*error_out = "failed to decode interleaved audio sample";
				}
				return false;
			}
			decoded_audio->channels[static_cast<size_t>(channel)].push_back(decoded_sample);
		}
	}
	return true;
}

bool MixToMono(const std::vector<std::vector<float>>& input_channels, std::vector<float>* mono_output, std::string* error_out) {
	if (mono_output == nullptr || input_channels.empty()) {
		if (error_out != nullptr) {
			*error_out = "audio channel mix target is invalid";
		}
		return false;
	}

	const size_t frame_count = input_channels.front().size();
	for (const auto& channel : input_channels) {
		if (channel.size() != frame_count) {
			if (error_out != nullptr) {
				*error_out = "audio channels do not share the same frame count";
			}
			return false;
		}
	}

	mono_output->assign(frame_count, 0.0f);
	for (size_t frame_index = 0; frame_index < frame_count; ++frame_index) {
		float sum = 0.0f;
		for (const auto& channel : input_channels) {
			sum += channel[frame_index];
		}
		(*mono_output)[frame_index] = sum / static_cast<float>(input_channels.size());
	}
	return true;
}

std::filesystem::path CreateTempPath(const std::string& extension) {
	const auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
	std::string normalized_extension = extension;
	if (!normalized_extension.empty() && normalized_extension.front() != '.') {
		normalized_extension.insert(normalized_extension.begin(), '.');
	}
	return std::filesystem::temp_directory_path() / ("xer_codec_" + std::to_string(now) + normalized_extension);
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

}  // namespace

bool DecodeAudioBufferWithFfmpeg(
	const uint8_t* data,
	size_t bytes,
	const std::string& extension,
	std::vector<float>* output,
	std::string* error_out) {
	if (data == nullptr || bytes == 0 || output == nullptr) {
		if (error_out != nullptr) {
			*error_out = "invalid audio buffer decode request";
		}
		return false;
	}
	if (!XER::Bridge::FFmpeg::IsAvailable()) {
		if (error_out != nullptr) {
			*error_out = "ffmpeg bridge is unavailable";
		}
		return false;
	}

	const TempFileGuard temp_path(CreateTempPath(extension));
	{
		std::ofstream output_stream(temp_path.path(), std::ios::binary);
		if (!output_stream.is_open()) {
			if (error_out != nullptr) {
				*error_out = "failed to create temporary audio file";
			}
			return false;
		}
		output_stream.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(bytes));
		if (!output_stream) {
			if (error_out != nullptr) {
				*error_out = "failed to write temporary audio file";
			}
			return false;
		}
	}

	std::vector<XER::Bridge::FFmpeg::AudioFrameInfo> frames;
	if (!XER::Bridge::FFmpeg::DecodeAudioFrames(temp_path.path().string(), -1, 0, &frames, error_out)) {
		return false;
	}
	if (frames.empty()) {
		if (error_out != nullptr) {
			*error_out = "ffmpeg did not decode any audio frames";
		}
		return false;
	}

	DecodedAudioData decoded_audio;
	for (const auto& frame : frames) {
		if (frame.sample_rate <= 0) {
			if (error_out != nullptr) {
				*error_out = "decoded audio frame is missing sample rate";
			}
			return false;
		}
		if (decoded_audio.sample_rate == 0) {
			decoded_audio.sample_rate = frame.sample_rate;
		} else if (decoded_audio.sample_rate != frame.sample_rate) {
			if (error_out != nullptr) {
				*error_out = "decoded audio frames use inconsistent sample rates";
			}
			return false;
		}
		if (!AppendFrameChannels(frame, &decoded_audio, error_out)) {
			return false;
		}
	}

	if (!MixToMono(decoded_audio.channels, output, error_out)) {
		return false;
	}
	return !output->empty();
}

}  // namespace XER::Audio::CodecIO::detail