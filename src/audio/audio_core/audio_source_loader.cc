#include "audio_source_loader.h"

#include "audio/audio_core/audio_interleave_processor.h"
#include "audio/file_io_codecs/codec_wav_pcm.h"
#include "wrapper/ffmpeg/ffmpeg_engine_bridge.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <future>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace Engine::Audio::Core {

namespace {

struct DecodedAudioData {
	std::vector<std::vector<float>> channels;
	int sample_rate = 0;
	std::string source_format;
	std::string codec_name;
	std::string decode_backend;

	size_t FrameCount() const {
		return channels.empty() ? 0 : channels.front().size();
	}
};

unsigned CpuWorkerCount(size_t work_items) {
	if (work_items == 0u) return 1u;
	const unsigned hardware = std::thread::hardware_concurrency();
	return static_cast<unsigned>(std::min<size_t>(work_items, std::max(1u, hardware)));
}

template <typename Callback>
void RunParallelRanges(size_t item_count, size_t minimum_items_per_worker, Callback&& callback) {
	if (item_count == 0u) return;
	const size_t worker_count = std::min<size_t>(
		CpuWorkerCount(item_count),
		std::max<size_t>(1u, item_count / std::max<size_t>(1u, minimum_items_per_worker)));
	if (worker_count <= 1u) {
		callback(0u, item_count);
		return;
	}

	std::vector<std::future<void>> futures;
	futures.reserve(worker_count - 1u);
	for (size_t worker = 1u; worker < worker_count; ++worker) {
		const size_t begin = item_count * worker / worker_count;
		const size_t end = item_count * (worker + 1u) / worker_count;
		futures.push_back(std::async(std::launch::async, callback, begin, end));
	}
	callback(0u, item_count / worker_count);
	for (auto& future : futures) {
		future.get();
	}
}

enum class SampleFormatKind {
	Unknown,
	U8,
	S16,
	S32,
	S64,
	F32,
	F64,
};

std::string ToLowerCopy(std::string value) {
	std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
		return static_cast<char>(std::tolower(ch));
	});
	return value;
}

bool ReadBinaryFile(
	const std::filesystem::path& path,
	std::vector<std::uint8_t>* bytes_out,
	std::string* error_out) {
	if (bytes_out == nullptr) {
		if (error_out != nullptr) {
			*error_out = "audio byte target is null";
		}
		return false;
	}

	std::ifstream input(path, std::ios::binary | std::ios::ate);
	if (!input.is_open()) {
		if (error_out != nullptr) {
			*error_out = "failed to open audio source: " + path.string();
		}
		return false;
	}

	const std::streamsize size = input.tellg();
	if (size < 0) {
		if (error_out != nullptr) {
			*error_out = "failed to size audio source: " + path.string();
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
			*error_out = "failed to read audio source: " + path.string();
		}
		return false;
	}
	return true;
}

bool ReadFilePrefix(
	const std::filesystem::path& path,
	std::vector<std::uint8_t>* bytes_out,
	std::string* error_out) {
	if (bytes_out == nullptr) {
		if (error_out != nullptr) {
			*error_out = "audio header target is null";
		}
		return false;
	}

	std::ifstream input(path, std::ios::binary);
	if (!input.is_open()) {
		if (error_out != nullptr) {
			*error_out = "failed to open audio source: " + path.string();
		}
		return false;
	}

	std::array<std::uint8_t, 4096> prefix{};
	input.read(reinterpret_cast<char*>(prefix.data()), static_cast<std::streamsize>(prefix.size()));
	const std::streamsize bytes_read = input.gcount();
	if (bytes_read <= 0) {
		if (error_out != nullptr) {
			*error_out = "audio source is empty: " + path.string();
		}
		return false;
	}
	bytes_out->assign(prefix.begin(), prefix.begin() + static_cast<std::ptrdiff_t>(bytes_read));
	return true;
}

bool LooksLikeMp3FrameHeader(const std::uint8_t* data, size_t bytes) {
	if (data == nullptr || bytes < 4u) {
		return false;
	}
	for (size_t index = 0; index + 3u < bytes; ++index) {
		const std::uint8_t b0 = data[index];
		const std::uint8_t b1 = data[index + 1u];
		const std::uint8_t b2 = data[index + 2u];
		if (b0 != 0xFFu || (b1 & 0xE0u) != 0xE0u) {
			continue;
		}
		const std::uint8_t version = (b1 >> 3u) & 0x03u;
		const std::uint8_t layer = (b1 >> 1u) & 0x03u;
		const std::uint8_t bitrate = (b2 >> 4u) & 0x0Fu;
		const std::uint8_t sample_rate = (b2 >> 2u) & 0x03u;
		if (version == 0x01u || layer == 0x00u || bitrate == 0x00u || bitrate == 0x0Fu || sample_rate == 0x03u) {
			continue;
		}
		return true;
	}
	return false;
}

bool IsStrictMp3Input(const std::filesystem::path& path, std::string* error_out) {
	if (ToLowerCopy(path.extension().string()) != ".mp3") {
		if (error_out != nullptr) {
			*error_out = "audio fx processing accepts only strict .mp3 input: " + path.string();
		}
		return false;
	}

	std::vector<std::uint8_t> prefix;
	if (!ReadFilePrefix(path, &prefix, error_out)) {
		return false;
	}
	const bool has_id3 = prefix.size() >= 3u && prefix[0] == 'I' && prefix[1] == 'D' && prefix[2] == '3';
	if (!has_id3 && !LooksLikeMp3FrameHeader(prefix.data(), prefix.size())) {
		if (error_out != nullptr) {
			*error_out = "audio fx processing accepts only valid MP3 frame streams: " + path.string();
		}
		return false;
	}
	return true;
}

std::string CanonicalSampleFormat(std::string sample_format) {
	sample_format = ToLowerCopy(std::move(sample_format));
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

SampleFormatKind ParseSampleFormat(const std::string& sample_format) {
	const std::string canonical = CanonicalSampleFormat(sample_format);
	if (canonical == "u8") return SampleFormatKind::U8;
	if (canonical == "s16") return SampleFormatKind::S16;
	if (canonical == "s32") return SampleFormatKind::S32;
	if (canonical == "s64") return SampleFormatKind::S64;
	if (canonical == "flt") return SampleFormatKind::F32;
	if (canonical == "dbl") return SampleFormatKind::F64;
	return SampleFormatKind::Unknown;
}

size_t BytesPerSample(SampleFormatKind sample_format) {
	switch (sample_format) {
	case SampleFormatKind::U8:
		return 1u;
	case SampleFormatKind::S16:
		return 2u;
	case SampleFormatKind::S32:
	case SampleFormatKind::F32:
		return 4u;
	case SampleFormatKind::S64:
	case SampleFormatKind::F64:
		return 8u;
	default:
		return 0u;
	}
}

template <typename SampleType>
SampleType ReadPod(const std::uint8_t* data) {
	SampleType value{};
	std::memcpy(&value, data, sizeof(SampleType));
	return value;
}

bool DecodeSample(SampleFormatKind sample_format, const std::uint8_t* data, float* sample_out) {
	if (data == nullptr || sample_out == nullptr) {
		return false;
	}

	if (sample_format == SampleFormatKind::U8) {
		*sample_out = (static_cast<float>(*data) - 128.0f) / 128.0f;
		return true;
	}
	if (sample_format == SampleFormatKind::S16) {
		*sample_out = static_cast<float>(ReadPod<std::int16_t>(data)) / 32768.0f;
		return true;
	}
	if (sample_format == SampleFormatKind::S32) {
		*sample_out = static_cast<float>(static_cast<double>(ReadPod<std::int32_t>(data)) / 2147483648.0);
		return true;
	}
	if (sample_format == SampleFormatKind::S64) {
		*sample_out = static_cast<float>(static_cast<long double>(ReadPod<std::int64_t>(data)) / 9223372036854775808.0L);
		return true;
	}
	if (sample_format == SampleFormatKind::F32) {
		*sample_out = ReadPod<float>(data);
		return true;
	}
	if (sample_format == SampleFormatKind::F64) {
		*sample_out = static_cast<float>(ReadPod<double>(data));
		return true;
	}
	return false;
}

bool DecodeSample(const std::string& sample_format, const std::uint8_t* data, float* sample_out) {
	return DecodeSample(ParseSampleFormat(sample_format), data, sample_out);
}

bool AppendFrameChannels(
	const engine::bridge::ffmpeg::AudioFrameInfo& frame,
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

	const SampleFormatKind sample_format = ParseSampleFormat(frame.sample_format);
	const size_t bytes_per_sample = BytesPerSample(sample_format);
	if (bytes_per_sample == 0) {
		if (error_out != nullptr) {
			*error_out = "unsupported ffmpeg sample format: " + frame.sample_format;
		}
		return false;
	}

	std::vector<size_t> write_offsets(decoded_audio->channels.size(), 0u);
	for (size_t channel = 0; channel < decoded_audio->channels.size(); ++channel) {
		write_offsets[channel] = decoded_audio->channels[channel].size();
		decoded_audio->channels[channel].resize(write_offsets[channel] + static_cast<size_t>(frame.sample_count));
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
			float* output = decoded_audio->channels[static_cast<size_t>(channel)].data() + write_offsets[static_cast<size_t>(channel)];
			for (int sample_index = 0; sample_index < frame.sample_count; ++sample_index) {
				const size_t byte_offset = plane_offsets[static_cast<size_t>(channel)]
					+ static_cast<size_t>(sample_index) * bytes_per_sample;
				if (!DecodeSample(sample_format, frame.data.data() + static_cast<std::ptrdiff_t>(byte_offset), output + sample_index)) {
					if (error_out != nullptr) {
						*error_out = "failed to decode planar audio sample";
					}
					return false;
				}
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
			const size_t byte_offset = (static_cast<size_t>(sample_index) * static_cast<size_t>(frame.channels)
				+ static_cast<size_t>(channel)) * bytes_per_sample;
			float* output = decoded_audio->channels[static_cast<size_t>(channel)].data() + write_offsets[static_cast<size_t>(channel)];
			if (!DecodeSample(sample_format, frame.data.data() + static_cast<std::ptrdiff_t>(byte_offset), output + sample_index)) {
				if (error_out != nullptr) {
					*error_out = "failed to decode interleaved audio sample";
				}
				return false;
			}
		}
	}
	return true;
}

bool DecodeRawPcmFile(
	const std::filesystem::path& path,
	int sample_rate,
	DecodedAudioData* decoded_audio,
	std::string* error_out) {
	if (decoded_audio == nullptr || sample_rate <= 0) {
		if (error_out != nullptr) {
			*error_out = "raw pcm decode target is invalid";
		}
		return false;
	}

	std::vector<std::uint8_t> bytes;
	if (!ReadBinaryFile(path, &bytes, error_out)) {
		return false;
	}
	if (bytes.empty() || (bytes.size() % 2u) != 0u) {
		if (error_out != nullptr) {
			*error_out = "raw pcm input must contain 16-bit mono samples";
		}
		return false;
	}

	decoded_audio->channels.assign(1, std::vector<float>(bytes.size() / 2u, 0.0f));
	RunParallelRanges(decoded_audio->channels.front().size(), 65536u, [&](size_t begin, size_t end) {
		for (size_t index = begin; index < end; ++index) {
			const std::int16_t sample = static_cast<std::int16_t>(
				static_cast<std::uint16_t>(bytes[index * 2u])
				| (static_cast<std::uint16_t>(bytes[index * 2u + 1u]) << 8u));
			decoded_audio->channels.front()[index] = static_cast<float>(sample) / 32768.0f;
		}
	});
	decoded_audio->sample_rate = sample_rate;
	decoded_audio->source_format = "raw_s16le_mono";
	decoded_audio->codec_name = "pcm_s16le";
	decoded_audio->decode_backend = "manual";
	return true;
}

bool DecodeWavPcmFile(
	const std::filesystem::path& path,
	DecodedAudioData* decoded_audio,
	std::string* error_out) {
	if (decoded_audio == nullptr) {
		if (error_out != nullptr) {
			*error_out = "wav decode target is invalid";
		}
		return false;
	}

	std::vector<std::uint8_t> bytes;
	if (!ReadBinaryFile(path, &bytes, error_out)) {
		return false;
	}

	Engine::Audio::CodecIO::WavPcmCodec codec;
	int sample_rate = 0;
	int channel_count = 0;
	std::vector<float> interleaved_samples;
	if (!codec.Decode16(bytes.data(), bytes.size(), interleaved_samples, &sample_rate, &channel_count)) {
		if (error_out != nullptr) {
			*error_out = "failed to decode PCM WAV input";
		}
		return false;
	}
	if (channel_count <= 0 || interleaved_samples.empty() || (interleaved_samples.size() % static_cast<size_t>(channel_count)) != 0u) {
		if (error_out != nullptr) {
			*error_out = "decoded PCM WAV payload is invalid";
		}
		return false;
	}

	const size_t frame_count = interleaved_samples.size() / static_cast<size_t>(channel_count);
	if (!AudioInterleaveProcessor::DeinterleavePlanar(
			interleaved_samples.data(),
			channel_count,
			frame_count,
			decoded_audio->channels)) {
		if (error_out != nullptr) {
			*error_out = "failed to deinterleave PCM WAV input";
		}
		return false;
	}

	decoded_audio->sample_rate = sample_rate;
	decoded_audio->source_format = "wav";
	decoded_audio->codec_name = "pcm_s16le";
	decoded_audio->decode_backend = "local_wav";
	return decoded_audio->FrameCount() > 0;
}

bool DecodeWithFFmpeg(
	const std::filesystem::path& path,
	DecodedAudioData* decoded_audio,
	std::string* error_out) {
	if (decoded_audio == nullptr) {
		if (error_out != nullptr) {
			*error_out = "ffmpeg decode target is invalid";
		}
		return false;
	}
	if (!engine::bridge::ffmpeg::IsAvailable()) {
		if (error_out != nullptr) {
			*error_out = "ffmpeg bridge is unavailable";
		}
		return false;
	}

	engine::bridge::ffmpeg::MediaInfo media_info;
	const bool have_media_info = engine::bridge::ffmpeg::ProbeMedia(path.string(), &media_info, nullptr);

	std::vector<engine::bridge::ffmpeg::AudioFrameInfo> frames;
	if (!engine::bridge::ffmpeg::DecodeAudioFrames(path.string(), -1, 0, &frames, error_out)) {
		return false;
	}
	if (frames.empty()) {
		if (error_out != nullptr) {
			*error_out = "ffmpeg did not decode any audio frames";
		}
		return false;
	}

	decoded_audio->channels.clear();
	decoded_audio->sample_rate = 0;
	int decoded_channels = 0;
	size_t total_frame_count = 0u;
	for (const auto& frame : frames) {
		if (frame.sample_rate <= 0) {
			if (error_out != nullptr) {
				*error_out = "decoded audio frame is missing sample rate";
			}
			return false;
		}
		if (decoded_audio->sample_rate == 0) {
			decoded_audio->sample_rate = frame.sample_rate;
		} else if (decoded_audio->sample_rate != frame.sample_rate) {
			if (error_out != nullptr) {
				*error_out = "decoded audio frames use inconsistent sample rates";
			}
			return false;
		}
		if (decoded_channels == 0) {
			decoded_channels = frame.channels;
		} else if (decoded_channels != frame.channels) {
			if (error_out != nullptr) {
				*error_out = "decoded audio frames use inconsistent channel counts";
			}
			return false;
		}
		total_frame_count += static_cast<size_t>(frame.sample_count);
	}
	decoded_audio->channels.assign(static_cast<size_t>(decoded_channels), {});
	for (auto& channel : decoded_audio->channels) {
		channel.reserve(total_frame_count);
	}
	for (const auto& frame : frames) {
		if (!AppendFrameChannels(frame, decoded_audio, error_out)) {
			return false;
		}
	}

	decoded_audio->source_format = have_media_info && !media_info.format_name.empty()
		? media_info.format_name
		: "ffmpeg";
	decoded_audio->codec_name.clear();
	if (have_media_info) {
		for (const auto& stream : media_info.streams) {
			if (stream.media_type == "audio") {
				decoded_audio->codec_name = stream.codec_name;
				break;
			}
		}
	}
	decoded_audio->decode_backend = "ffmpeg";
	return decoded_audio->FrameCount() > 0;
}

bool MixToMono(
	const std::vector<std::vector<float>>& input_channels,
	std::vector<float>* mono_output,
	std::string* error_out) {
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
				*error_out = "decoded audio channels are not aligned";
			}
			return false;
		}
	}

	if (input_channels.size() == 1u) {
		*mono_output = input_channels.front();
		return true;
	}

	mono_output->assign(frame_count, 0.0f);
	std::vector<const float*> channel_ptrs(input_channels.size(), nullptr);
	for (size_t index = 0; index < input_channels.size(); ++index) {
		channel_ptrs[index] = input_channels[index].data();
	}

	if (!AudioInterleaveProcessor::MixChannels(
			channel_ptrs.data(),
			static_cast<int>(input_channels.size()),
			mono_output->data(),
			frame_count)) {
		if (error_out != nullptr) {
			*error_out = "failed to mix decoded audio channels";
		}
		return false;
	}
	return true;
}

bool ResampleMono(
	const std::vector<float>& input,
	int input_sample_rate,
	int output_sample_rate,
	ResampleQuality quality,
	std::vector<float>* output,
	std::string* error_out) {
	if (output == nullptr || input_sample_rate <= 0 || output_sample_rate <= 0) {
		if (error_out != nullptr) {
			*error_out = "audio resample configuration is invalid";
		}
		return false;
	}

	if (input.empty()) {
		output->clear();
		return true;
	}

	if (input_sample_rate == output_sample_rate) {
		*output = input;
		return true;
	}

	AudioSampleRateConverter converter(quality);
	if (!converter.Initialize(input_sample_rate, output_sample_rate)) {
		if (error_out != nullptr) {
			*error_out = "failed to initialize audio sample-rate converter";
		}
		return false;
	}

	const size_t predicted_frames = std::max<size_t>(
		1u,
		static_cast<size_t>(std::ceil(static_cast<double>(input.size()) * converter.GetRatio())));
	output->assign(predicted_frames, 0.0f);

	size_t output_frames = 0;
	if (!converter.Convert(input.data(), input.size(), output->data(), output_frames)) {
		if (error_out != nullptr) {
			*error_out = "failed to resample normalized audio";
		}
		return false;
	}
	output->resize(output_frames);
	return !output->empty();
}

bool ResamplePlanar(
	const std::vector<std::vector<float>>& input_channels,
	int input_sample_rate,
	int output_sample_rate,
	ResampleQuality quality,
	std::vector<std::vector<float>>* output_channels,
	std::string* error_out) {
	if (output_channels == nullptr || input_channels.empty()) {
		if (error_out != nullptr) {
			*error_out = "audio planar resample target is invalid";
		}
		return false;
	}

	output_channels->assign(input_channels.size(), {});
	if (input_channels.size() == 1u) {
		return ResampleMono(input_channels.front(), input_sample_rate, output_sample_rate, quality, &output_channels->front(), error_out);
	}

	std::vector<std::string> errors(input_channels.size());
	std::vector<uint8_t> ok(input_channels.size(), 0u);
	RunParallelRanges(input_channels.size(), 1u, [&](size_t begin, size_t end) {
		for (size_t channel = begin; channel < end; ++channel) {
			ok[channel] = ResampleMono(
				input_channels[channel],
				input_sample_rate,
				output_sample_rate,
				quality,
				&(*output_channels)[channel],
				&errors[channel]) ? 1u : 0u;
		}
	});
	for (size_t channel = 0; channel < input_channels.size(); ++channel) {
		if (!ok[channel]) {
			if (error_out != nullptr) {
				*error_out = errors[channel].empty() ? "failed to resample normalized audio channel" : errors[channel];
			}
			return false;
		}
	}
	return true;
}

bool AdjustChannelCount(
	const std::vector<std::vector<float>>& input_channels,
	int target_channels,
	std::vector<std::vector<float>>* output_channels,
	std::string* error_out) {
	if (output_channels == nullptr || input_channels.empty() || target_channels == 0) {
		if (error_out != nullptr) {
			*error_out = "audio channel conversion target is invalid";
		}
		return false;
	}

	const size_t input_channel_count = input_channels.size();
	const size_t frame_count = input_channels.front().size();
	for (const auto& channel : input_channels) {
		if (channel.size() != frame_count) {
			if (error_out != nullptr) {
				*error_out = "normalized audio channels are not aligned";
			}
			return false;
		}
	}

	if (target_channels < 0) {
		*output_channels = input_channels;
		return true;
	}
	if (target_channels == 1) {
		std::vector<float> mono_output;
		if (!MixToMono(input_channels, &mono_output, error_out)) {
			return false;
		}
		output_channels->assign(1, std::move(mono_output));
		return true;
	}

	output_channels->assign(static_cast<size_t>(target_channels), std::vector<float>(frame_count, 0.0f));
	for (int channel = 0; channel < target_channels; ++channel) {
		const size_t source_index = std::min(static_cast<size_t>(channel), input_channel_count - 1u);
		(*output_channels)[static_cast<size_t>(channel)] = input_channels[source_index];
	}
	return true;
}

bool InterleaveChannels(
	const std::vector<std::vector<float>>& input_channels,
	std::vector<float>* interleaved_output,
	std::string* error_out) {
	if (interleaved_output == nullptr || input_channels.empty()) {
		if (error_out != nullptr) {
			*error_out = "audio interleave target is invalid";
		}
		return false;
	}
	const size_t frame_count = input_channels.front().size();
	for (const auto& channel : input_channels) {
		if (channel.size() != frame_count) {
			if (error_out != nullptr) {
				*error_out = "audio interleave channels are not aligned";
			}
			return false;
		}
	}

	interleaved_output->assign(frame_count * input_channels.size(), 0.0f);
	std::vector<const float*> channel_ptrs(input_channels.size(), nullptr);
	for (size_t channel = 0; channel < input_channels.size(); ++channel) {
		channel_ptrs[channel] = input_channels[channel].data();
	}
	if (!AudioInterleaveProcessor::Interleave(
			channel_ptrs.data(),
			static_cast<int>(input_channels.size()),
			frame_count,
			interleaved_output->data())) {
		if (error_out != nullptr) {
			*error_out = "failed to interleave normalized audio channels";
		}
		return false;
	}
	return true;
}

}  // namespace

bool AudioSourceLoader::Load(
	const AudioSourceLoadOptions& options,
	AudioSourceBuffer* output,
	std::string* error_out) {
	if (output == nullptr) {
		if (error_out != nullptr) {
			*error_out = "audio source output target is null";
		}
		return false;
	}
	if (options.input_path.empty()) {
		if (error_out != nullptr) {
			*error_out = "audio input path is empty";
		}
		return false;
	}
	if (!std::filesystem::exists(options.input_path)) {
		if (error_out != nullptr) {
			*error_out = "audio input not found: " + options.input_path.string();
		}
		return false;
	}
	if (options.strict_mp3_input && !IsStrictMp3Input(options.input_path, error_out)) {
		return false;
	}
	if (options.target_sample_rate <= 0 || options.raw_sample_rate <= 0) {
		if (error_out != nullptr) {
			*error_out = "audio normalization sample rate must be positive";
		}
		return false;
	}

	DecodedAudioData decoded_audio;
	const std::string extension = ToLowerCopy(options.input_path.extension().string());
	if (extension == ".raw" || extension == ".pcm") {
		if (!DecodeRawPcmFile(options.input_path, options.raw_sample_rate, &decoded_audio, error_out)) {
			return false;
		}
	} else {
		std::string ffmpeg_error;
		std::string wav_error;
		bool decoded = false;
		if (extension == ".wav") {
			decoded = DecodeWavPcmFile(options.input_path, &decoded_audio, &wav_error);
		}
		if (!decoded && engine::bridge::ffmpeg::IsAvailable()) {
			decoded = DecodeWithFFmpeg(options.input_path, &decoded_audio, &ffmpeg_error);
		}
		if (!decoded) {
			if (error_out != nullptr) {
				*error_out = !wav_error.empty() ? wav_error : (!ffmpeg_error.empty() ? ffmpeg_error : "no decoder available for input audio");
			}
			return false;
		}
	}

	const int target_channels = options.target_channels <= 0
		? static_cast<int>(decoded_audio.channels.size())
		: options.target_channels;
	if (target_channels <= 0) {
		if (error_out != nullptr) {
			*error_out = "audio normalization target channel count is invalid";
		}
		return false;
	}

	std::vector<std::vector<float>> resampled_channels;
	if (!ResamplePlanar(
			decoded_audio.channels,
			decoded_audio.sample_rate,
			options.target_sample_rate,
			options.resample_quality,
			&resampled_channels,
			error_out)) {
		return false;
	}

	std::vector<std::vector<float>> normalized_channels;
	if (!AdjustChannelCount(resampled_channels, target_channels, &normalized_channels, error_out)) {
		return false;
	}

	std::vector<float> normalized_audio;
	if (!InterleaveChannels(normalized_channels, &normalized_audio, error_out)) {
		return false;
	}

	output->samples = std::move(normalized_audio);
	output->sample_rate = options.target_sample_rate;
	output->channels = target_channels;
	output->original_sample_rate = decoded_audio.sample_rate;
	output->original_channels = static_cast<int>(decoded_audio.channels.size());
	output->frame_count = normalized_channels.empty() ? 0u : normalized_channels.front().size();
	output->original_frame_count = decoded_audio.FrameCount();
	output->source_format = decoded_audio.source_format;
	output->codec_name = decoded_audio.codec_name;
	output->decode_backend = decoded_audio.decode_backend;
	return !output->samples.empty();
}

}  // namespace Engine::Audio::Core