#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace XER::Bridge::FFmpeg {

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
	// AVStream::disposition & AV_DISPOSITION_ATTACHED_PIC — embedded
	// cover art (id3v2 APIC / FLAC picture block / mp4 covr). A
	// "video" stream with this flag is a picture belonging to the
	// audio, never movie frames — classification must not treat it
	// as the primary video stream.
	bool attached_pic = false;
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
    // Optional constant-quality target forwarded to the encoder's
    // private "crf" option (libx264/libx265 style). -1 keeps the
    // bit_rate path; encoders without a crf option ignore it silently.
    int crf = -1;
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
	// FFmpeg channel-layout bitmask (AV_CHANNEL_LAYOUT_*): which
	// speaker position each channel index carries. 0 = unknown —
	// consumers fall back to channel-count heuristics.
	std::int64_t channel_layout = 0;
	int sample_count = 0;
	bool planar = false;
	std::string sample_format;
	std::int64_t pts = 0;
	std::int64_t best_effort_timestamp = 0;
	std::vector<int> plane_sizes;
	std::vector<uint8_t> data;
};

using AudioFrameStreamProvider =
    std::function<bool(std::size_t frame_index, AudioFrameInfo* out_frame,
                       std::string* error)>;

// FFmpeg log verbosity (maps to av_log levels). Libraries stay quiet by
// default at AV_LOG_INFO; tools can raise/lower per process.
enum class LogLevel {
	kQuiet,
	kError,
	kWarning,
	kInfo,
	kDebug,
};
void SetLogLevel(LogLevel level);

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
// Streams provider-produced PCM chunks into a single audio-only
// container (params.output_path authoritative). frame_count chunks are
// requested with frame_index in [0, frame_count) exactly once; chunk 0
// is fetched during encoder setup to fix the input format, and EVERY
// chunk must share sample_rate/channels/sample_format/planar (the
// encoder rejects inconsistent streams, same contract as
// EncodeAudioFrames). frame_count == 0 is an error; a false provider
// return aborts with error_out set. On failure a partial file may
// remain at output_path — callers doing atomic output must encode to a
// temp path (ExportEngine does).
bool EncodeAudioStream(const EncodeAudioParams& params,
                       std::size_t frame_count,
                       const AudioFrameStreamProvider& provider,
                       std::string* error_out);
struct EncodeAudioVideoParams {
    EncodeVideoParams video;
    EncodeAudioParams audio;
};

// Streams interleaved video+audio into ONE container: a single
// AVFormatContext with one video stream (params.video) and, when
// audio_frame_count > 0, one audio stream (params.audio). Output path
// comes from params.video.output_path; params.audio.output_path is
// ignored. video_frame_count == 0 is an error; audio_frame_count == 0
// produces a video-only file. Providers are invoked with frame_index in
// [0, count) exactly once; a false return aborts with error_out set.
bool EncodeAudioVideoStream(
    const EncodeAudioVideoParams& params,
    std::size_t video_frame_count, const VideoFrameProvider& video_provider,
    std::size_t audio_frame_count,
    const AudioFrameStreamProvider& audio_provider,
    std::string* error_out);
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

// Decodes the first frame of an image file (any format FFmpeg's image2
// demuxer handles: png/jpg/bmp/webp/...), scales it so the longest side is
// at most max_size pixels (0 = keep original size, dims forced even), and
// writes a JPEG to output_path.
bool ImageToJpeg(const std::string& input_path,
                 const std::string& output_path,
                 int max_size,
                 std::string* out_error);

bool ExtractStream(const std::string& input_path,
				   int stream_index,
				   const std::string& output_path,
				   std::string* out_error);

}  // namespace XER::Bridge::FFmpeg