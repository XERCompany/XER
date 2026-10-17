#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace engine::bridge::ffmpeg {

struct LibraryVersion {
	std::string name;
	unsigned version = 0;
	int major = 0;
	int minor = 0;
	int micro = 0;
	std::string version_text;
};

struct NamedItem {
	std::string name;
	std::string description;
};

struct ProtocolInfo {
	std::string name;
	bool input = false;
	bool output = false;
};

struct CodecInfo {
	std::string name;
	std::string long_name;
	std::string media_type;
	bool encoder = false;
	bool decoder = false;
	bool intra_only = false;
	bool lossy = false;
	bool lossless = false;
};

struct FilterInfo {
	std::string name;
	std::string description;
	bool dynamic_inputs = false;
	bool dynamic_outputs = false;
	bool slice_threads = false;
	bool timeline_generic = false;
	bool timeline_internal = false;
	bool timeline_support = false;
};

struct DeviceInfo {
	std::string name;
	std::string description;
	std::string media_type;
	bool input = false;
	bool output = false;
};

struct MetadataEntry {
	std::string key;
	std::string value;
};

struct StreamInfo {
	int index = -1;
	std::string media_type;
	std::string codec_name;
	std::string codec_long_name;
	int codec_id = 0;
	int width = 0;
	int height = 0;
	int sample_rate = 0;
	int channels = 0;
	std::int64_t channel_layout = 0;
	std::string sample_format;
	std::string pixel_format;
	int time_base_num = 0;
	int time_base_den = 1;
	std::int64_t duration = 0;
	std::int64_t start_time = 0;
	std::int64_t bit_rate = 0;
	std::int64_t frame_count = 0;
	std::string language;
	std::vector<MetadataEntry> metadata;
};

struct MediaInfo {
	std::string path;
	std::string format_name;
	std::string format_long_name;
	std::int64_t duration = 0;
	std::int64_t start_time = 0;
	std::int64_t size = 0;
	std::int64_t bit_rate = 0;
	int stream_count = 0;
	std::vector<MetadataEntry> metadata;
	std::vector<StreamInfo> streams;
};

struct PacketInfo {
	int stream_index = -1;
	std::string media_type;
	std::int64_t pts = 0;
	std::int64_t dts = 0;
	std::int64_t duration = 0;
	std::int64_t pos = 0;
	int size = 0;
	bool key_frame = false;
	bool corrupt = false;
};


struct EncodeVideoParams {
    std::string output_path;
    std::string codec_name;
    int width = 0;
    int height = 0;
    std::string pixel_format;
    int fps_num = 25;
    int fps_den = 1;
    int bit_rate = 0;
};

struct EncodeAudioParams {
    std::string output_path;
    std::string codec_name;
    int sample_rate = 0;
    int channels = 0;
    std::string sample_format;
    int bit_rate = 0;
};

struct TranscodingParams {
    std::string input_path;
    std::string output_path;
    std::string video_codec;
    std::string audio_codec;
    int video_bitrate = 0;
    int audio_bitrate = 0;
    int width = 0;
    int height = 0;
    int sample_rate = 0;
    int channels = 0;
};

struct VideoFrameInfo {
	int stream_index = -1;
	int width = 0;
	int height = 0;
	std::string pixel_format;
	std::string picture_type;
	std::int64_t pts = 0;
	std::int64_t best_effort_timestamp = 0;
	std::int64_t duration = 0;
	bool key_frame = false;
	std::vector<int> line_sizes;
	std::vector<uint8_t> data;
};

using VideoFrameProvider = std::function<bool(std::size_t frame_index,
											 VideoFrameInfo* out_frame,
											 std::string* out_error)>;

struct AudioFrameInfo {
	int stream_index = -1;
	int sample_rate = 0;
	int channels = 0;
	int sample_count = 0;
	bool planar = false;
	std::string sample_format;
	std::int64_t pts = 0;
	std::int64_t best_effort_timestamp = 0;
	std::vector<int> plane_sizes;
	std::vector<uint8_t> data;
};

bool IsAvailable();
bool HasFilterLibrary();
bool HasDeviceLibrary();

std::string Summary();
std::string Configuration();
std::string License();

std::vector<LibraryVersion> LibraryVersions();
std::vector<ProtocolInfo> Protocols();
std::vector<NamedItem> InputFormats();
std::vector<NamedItem> OutputFormats();
std::vector<CodecInfo> Codecs();
std::vector<NamedItem> BitstreamFilters();
std::vector<FilterInfo> Filters();
std::vector<DeviceInfo> InputDevices();
std::vector<DeviceInfo> OutputDevices();

bool ProbeMedia(const std::string& path, MediaInfo* out_info, std::string* out_error);
bool ReadPackets(const std::string& path,
				 int max_packets,
				 std::vector<PacketInfo>* out_packets,
				 std::string* out_error);
bool DecodeVideoFrames(const std::string& path,
					   int stream_index,
					   int max_frames,
					   std::vector<VideoFrameInfo>* out_frames,
					   std::string* out_error);
bool DecodeAudioFrames(const std::string& path,
					   int stream_index,
					   int max_frames,
					   std::vector<AudioFrameInfo>* out_frames,
					   std::string* out_error);
bool RemuxCopy(const std::string& input_path,
			   const std::string& output_path,
			   std::string* out_error);

bool EncodeVideoFrames(const EncodeVideoParams& params,
                       const std::vector<VideoFrameInfo>& frames,
                       std::string* out_error);
bool EncodeVideoFrameStream(const EncodeVideoParams& params,
							std::size_t frame_count,
							const VideoFrameProvider& provider,
							std::string* out_error);
bool EncodeAudioFrames(const EncodeAudioParams& params,
                       const std::vector<AudioFrameInfo>& frames,
                       std::string* out_error);
bool RunFilterGraph(const std::string& input_path,
                    const std::string& output_path,
                    const std::string& filter_graph,
                    std::string* out_error);
bool CaptureDeviceToStream(const std::string& format_name,
                           const std::string& device_name,
                           const std::string& output_path,
                           int duration_seconds,
                           std::string* out_error);
bool Transcode(const TranscodingParams& params,
               std::string* out_error);

bool ExtractStream(const std::string& input_path,
				   int stream_index,
				   const std::string& output_path,
				   std::string* out_error);

}  // namespace engine::bridge::ffmpeg