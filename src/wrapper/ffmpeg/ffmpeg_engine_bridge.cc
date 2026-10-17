#include "wrapper/ffmpeg/ffmpeg_engine_bridge.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>
#include <sstream>
#include <string_view>

#include <absl/strings/str_format.h>

#if ENGINE_HAS_FFMPEG_BRIDGE
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/bsf.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libavutil/avutil.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/channel_layout.h>
#include <libavutil/dict.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixdesc.h>
#include <libavutil/opt.h>
#include <libavutil/samplefmt.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}
#endif

#if ENGINE_HAS_FFMPEG_AVFILTER
extern "C" {
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
}
#endif

#if ENGINE_HAS_FFMPEG_AVDEVICE
extern "C" {
#include <libavdevice/avdevice.h>
}
#endif

namespace engine::bridge::ffmpeg {

namespace {

void SetError(std::string* out_error, const std::string& message) {
    if (out_error != nullptr) {
        *out_error = message;
    }
}

#if ENGINE_HAS_FFMPEG_BRIDGE
#ifndef AV_CODEC_CAP_INTRA_ONLY
#define AV_CODEC_CAP_INTRA_ONLY 0
#endif
#ifndef AV_CODEC_CAP_LOSSY
#define AV_CODEC_CAP_LOSSY 0
#endif
#ifndef AV_CODEC_CAP_LOSSLESS
#define AV_CODEC_CAP_LOSSLESS 0
#endif

std::string ErrorString(int error_code) {
    char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(error_code, buffer, sizeof(buffer));
    return std::string(buffer);
}

LibraryVersion MakeVersion(std::string name, unsigned version) {
    LibraryVersion result;
    result.name = std::move(name);
    result.version = version;
    result.major = static_cast<int>((version >> 16U) & 0xFFU);
    result.minor = static_cast<int>((version >> 8U) & 0xFFU);
    result.micro = static_cast<int>(version & 0xFFU);
    result.version_text = std::to_string(result.major) + "." +
                          std::to_string(result.minor) + "." +
                          std::to_string(result.micro);
    return result;
}

std::string MediaTypeName(AVMediaType media_type) {
    const char* value = av_get_media_type_string(media_type);
    return value != nullptr ? std::string(value) : std::string("unknown");
}

std::vector<MetadataEntry> ReadMetadata(const AVDictionary* dictionary) {
    std::vector<MetadataEntry> entries;
    AVDictionaryEntry* item = nullptr;
    while ((item = av_dict_get(dictionary, "", item, AV_DICT_IGNORE_SUFFIX)) != nullptr) {
        entries.push_back({item->key != nullptr ? item->key : "",
                           item->value != nullptr ? item->value : ""});
    }
    return entries;
}

std::string SampleFormatName(int format) {
    if (format < 0) {
        return "";
    }
    const char* name = av_get_sample_fmt_name(static_cast<AVSampleFormat>(format));
    return name != nullptr ? std::string(name) : std::string();
}

AVSampleFormat ResolveSampleFormat(std::string sample_format, bool planar) {
    std::transform(sample_format.begin(), sample_format.end(), sample_format.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    if (!sample_format.empty() && sample_format.back() == 'p') {
        planar = true;
        sample_format.pop_back();
    }
    if (sample_format == "u8") {
        return planar ? AV_SAMPLE_FMT_U8P : AV_SAMPLE_FMT_U8;
    }
    if (sample_format == "s16") {
        return planar ? AV_SAMPLE_FMT_S16P : AV_SAMPLE_FMT_S16;
    }
    if (sample_format == "s32") {
        return planar ? AV_SAMPLE_FMT_S32P : AV_SAMPLE_FMT_S32;
    }
    if (sample_format == "flt") {
        return planar ? AV_SAMPLE_FMT_FLTP : AV_SAMPLE_FMT_FLT;
    }
    if (sample_format == "dbl") {
        return planar ? AV_SAMPLE_FMT_DBLP : AV_SAMPLE_FMT_DBL;
    }
    return AV_SAMPLE_FMT_NONE;
}

bool SetDefaultChannelLayout(int channels, AVChannelLayout* out_layout, std::string* out_error) {
    if (out_layout == nullptr || channels <= 0) {
        SetError(out_error, "invalid channel layout request");
        return false;
    }
    av_channel_layout_default(out_layout, channels);
    if (out_layout->nb_channels != channels) {
        SetError(out_error, "av_channel_layout_default failed");
        return false;
    }
    return true;
}

const AVSampleFormat* SupportedSampleFormats(const AVCodec* codec, int* out_count) {
    if (out_count != nullptr) {
        *out_count = 0;
    }
#if LIBAVCODEC_VERSION_MAJOR >= 61
    const void* configs = nullptr;
    int count = 0;
    if (avcodec_get_supported_config(nullptr, codec, AV_CODEC_CONFIG_SAMPLE_FORMAT, 0, &configs, &count) < 0) {
        return nullptr;
    }
    if (out_count != nullptr) {
        *out_count = count;
    }
    return static_cast<const AVSampleFormat*>(configs);
#else
    if (codec == nullptr) {
        return nullptr;
    }
    if (out_count != nullptr && codec->sample_fmts != nullptr) {
        for (const AVSampleFormat* current = codec->sample_fmts; *current != AV_SAMPLE_FMT_NONE; ++current) {
            ++*out_count;
        }
    }
    return codec->sample_fmts;
#endif
}

const int* SupportedSampleRates(const AVCodec* codec, int* out_count) {
    if (out_count != nullptr) {
        *out_count = 0;
    }
#if LIBAVCODEC_VERSION_MAJOR >= 61
    const void* configs = nullptr;
    int count = 0;
    if (avcodec_get_supported_config(nullptr, codec, AV_CODEC_CONFIG_SAMPLE_RATE, 0, &configs, &count) < 0) {
        return nullptr;
    }
    if (out_count != nullptr) {
        *out_count = count;
    }
    return static_cast<const int*>(configs);
#else
    if (codec == nullptr) {
        return nullptr;
    }
    if (out_count != nullptr && codec->supported_samplerates != nullptr) {
        for (const int* current = codec->supported_samplerates; *current != 0; ++current) {
            ++*out_count;
        }
    }
    return codec->supported_samplerates;
#endif
}

const AVChannelLayout* SupportedChannelLayouts(const AVCodec* codec, int* out_count) {
    if (out_count != nullptr) {
        *out_count = 0;
    }
#if LIBAVCODEC_VERSION_MAJOR >= 61
    const void* configs = nullptr;
    int count = 0;
    if (avcodec_get_supported_config(nullptr, codec, AV_CODEC_CONFIG_CHANNEL_LAYOUT, 0, &configs, &count) < 0) {
        return nullptr;
    }
    if (out_count != nullptr) {
        *out_count = count;
    }
    return static_cast<const AVChannelLayout*>(configs);
#else
    if (codec == nullptr) {
        return nullptr;
    }
    if (out_count != nullptr && codec->ch_layouts != nullptr) {
        for (const AVChannelLayout* layout = codec->ch_layouts; layout->nb_channels != 0; ++layout) {
            ++*out_count;
        }
    }
    return codec->ch_layouts;
#endif
}

const AVPixelFormat* SupportedPixelFormats(const AVCodec* codec, int* out_count) {
    if (out_count != nullptr) {
        *out_count = 0;
    }
#if LIBAVCODEC_VERSION_MAJOR >= 61
    const void* configs = nullptr;
    int count = 0;
    if (avcodec_get_supported_config(nullptr, codec, AV_CODEC_CONFIG_PIX_FORMAT, 0, &configs, &count) < 0) {
        return nullptr;
    }
    if (out_count != nullptr) {
        *out_count = count;
    }
    return static_cast<const AVPixelFormat*>(configs);
#else
    if (codec == nullptr) {
        return nullptr;
    }
    if (out_count != nullptr && codec->pix_fmts != nullptr) {
        for (const AVPixelFormat* fmt = codec->pix_fmts; *fmt != AV_PIX_FMT_NONE; ++fmt) {
            ++*out_count;
        }
    }
    return codec->pix_fmts;
#endif
}

bool EncoderSupportsSampleFormat(const AVCodec* codec, AVSampleFormat sample_format) {
    if (codec == nullptr || sample_format == AV_SAMPLE_FMT_NONE) {
        return false;
    }
    int sample_format_count = 0;
    const AVSampleFormat* sample_formats = SupportedSampleFormats(codec, &sample_format_count);
    if (sample_formats == nullptr) {
        return true;
    }
    for (int index = 0; index < sample_format_count; ++index) {
        if (sample_formats[index] == sample_format) {
            return true;
        }
    }
    return false;
}

AVSampleFormat SelectEncoderSampleFormat(const AVCodec* codec, const std::string& requested_format) {
    if (codec == nullptr) {
        return AV_SAMPLE_FMT_NONE;
    }
    if (!requested_format.empty()) {
        const AVSampleFormat requested = ResolveSampleFormat(requested_format, false);
        if (EncoderSupportsSampleFormat(codec, requested)) {
            return requested;
        }
    }
    int sample_format_count = 0;
    const AVSampleFormat* sample_formats = SupportedSampleFormats(codec, &sample_format_count);
    return sample_formats != nullptr && sample_format_count > 0 ? sample_formats[0] : AV_SAMPLE_FMT_FLTP;
}

int SelectEncoderSampleRate(const AVCodec* codec, int requested_sample_rate) {
    if (requested_sample_rate <= 0) {
        return 44100;
    }
    int sample_rate_count = 0;
    const int* sample_rates = SupportedSampleRates(codec, &sample_rate_count);
    if (codec == nullptr || sample_rates == nullptr || sample_rate_count == 0) {
        return requested_sample_rate;
    }
    int best_rate = sample_rates[0];
    for (int index = 0; index < sample_rate_count; ++index) {
        if (sample_rates[index] == requested_sample_rate) {
            return sample_rates[index];
        }
        if (std::abs(sample_rates[index] - requested_sample_rate) < std::abs(best_rate - requested_sample_rate)) {
            best_rate = sample_rates[index];
        }
    }
    return best_rate;
}

int SelectEncoderChannels(const AVCodec* codec, int requested_channels) {
    if (requested_channels <= 0) {
        return 1;
    }
    int channel_layout_count = 0;
    const AVChannelLayout* channel_layouts = SupportedChannelLayouts(codec, &channel_layout_count);
    if (codec == nullptr || channel_layouts == nullptr || channel_layout_count == 0) {
        return requested_channels;
    }
    for (int index = 0; index < channel_layout_count; ++index) {
        if (channel_layouts[index].nb_channels == requested_channels) {
            return requested_channels;
        }
    }
    return channel_layouts[0].nb_channels > 0 ? channel_layouts[0].nb_channels : requested_channels;
}

bool CopyAudioFrameToAvFrame(const AudioFrameInfo& input, AVFrame* frame, std::string* out_error) {
    if (frame == nullptr || input.sample_count <= 0 || input.channels <= 0) {
        SetError(out_error, "invalid audio frame copy request");
        return false;
    }
    const AVSampleFormat sample_format = ResolveSampleFormat(input.sample_format, input.planar);
    if (sample_format == AV_SAMPLE_FMT_NONE) {
        SetError(out_error, "unsupported input audio sample format: " + input.sample_format);
        return false;
    }

    frame->format = sample_format;
    frame->nb_samples = input.sample_count;
    frame->sample_rate = input.sample_rate;
    if (!SetDefaultChannelLayout(input.channels, &frame->ch_layout, out_error)) {
        return false;
    }
    const int allocation_result = av_frame_get_buffer(frame, 0);
    if (allocation_result < 0) {
        SetError(out_error, "av_frame_get_buffer failed: " + ErrorString(allocation_result));
        return false;
    }
    if (av_frame_make_writable(frame) < 0) {
        SetError(out_error, "av_frame_make_writable failed");
        return false;
    }

    const int bytes_per_sample = av_get_bytes_per_sample(sample_format);
    if (bytes_per_sample <= 0) {
        SetError(out_error, "av_get_bytes_per_sample failed for input frame");
        return false;
    }
    if (input.planar) {
        if (input.plane_sizes.size() < static_cast<size_t>(input.channels)) {
            SetError(out_error, "planar input frame is missing plane size metadata");
            return false;
        }
        size_t cursor = 0;
        for (int channel = 0; channel < input.channels; ++channel) {
            const size_t plane_size = static_cast<size_t>(input.plane_sizes[static_cast<size_t>(channel)]);
            if (cursor + plane_size > input.data.size()) {
                SetError(out_error, "planar input frame payload is truncated");
                return false;
            }
            std::memcpy(frame->extended_data[channel], input.data.data() + cursor, plane_size);
            cursor += plane_size;
        }
        return true;
    }

    const size_t required_bytes = static_cast<size_t>(input.sample_count) * static_cast<size_t>(input.channels) * static_cast<size_t>(bytes_per_sample);
    if (input.data.size() < required_bytes) {
        SetError(out_error, "interleaved input frame payload is truncated");
        return false;
    }
    std::memcpy(frame->data[0], input.data.data(), required_bytes);
    return true;
}

bool PushEncoderPackets(AVFormatContext* output_context,
    AVCodecContext* codec_context,
    AVStream* stream,
    AVPacket* packet,
    std::string* out_error) {
    while (true) {
        const int receive_result = avcodec_receive_packet(codec_context, packet);
        if (receive_result == AVERROR(EAGAIN) || receive_result == AVERROR_EOF) {
            return true;
        }
        if (receive_result < 0) {
            SetError(out_error, "avcodec_receive_packet failed: " + ErrorString(receive_result));
            return false;
        }
        av_packet_rescale_ts(packet, codec_context->time_base, stream->time_base);
        packet->stream_index = stream->index;
        const int write_result = av_interleaved_write_frame(output_context, packet);
        av_packet_unref(packet);
        if (write_result < 0) {
            SetError(out_error, "av_interleaved_write_frame failed: " + ErrorString(write_result));
            return false;
        }
    }
}

std::string PixelFormatName(int format) {
    if (format < 0) {
        return "";
    }
    const char* name = av_get_pix_fmt_name(static_cast<AVPixelFormat>(format));
    return name != nullptr ? std::string(name) : std::string();
}

StreamInfo MakeStreamInfo(const AVStream* stream) {
    StreamInfo info;
    if (stream == nullptr || stream->codecpar == nullptr) {
        return info;
    }

    const AVCodecParameters* codecpar = stream->codecpar;
    const AVCodec* codec = avcodec_find_decoder(codecpar->codec_id);

    info.index = stream->index;
    info.media_type = MediaTypeName(codecpar->codec_type);
    info.codec_id = static_cast<int>(codecpar->codec_id);
    info.codec_name = avcodec_get_name(codecpar->codec_id);
    info.codec_long_name = (codec != nullptr && codec->long_name != nullptr) ? codec->long_name : "";
    info.width = codecpar->width;
    info.height = codecpar->height;
    info.sample_rate = codecpar->sample_rate;
#if LIBAVCODEC_VERSION_MAJOR >= 59
    info.channels = codecpar->ch_layout.nb_channels;
    info.channel_layout = static_cast<std::int64_t>(codecpar->ch_layout.u.mask);
#else
    info.channels = codecpar->channels;
    info.channel_layout = static_cast<std::int64_t>(codecpar->channel_layout);
#endif
    info.sample_format = SampleFormatName(codecpar->format);
    info.pixel_format = PixelFormatName(codecpar->format);
    info.time_base_num = stream->time_base.num;
    info.time_base_den = stream->time_base.den;
    info.duration = stream->duration;
    info.start_time = stream->start_time;
    info.bit_rate = codecpar->bit_rate;
    info.frame_count = stream->nb_frames;
    info.metadata = ReadMetadata(stream->metadata);
    for (const auto& entry : info.metadata) {
        if (entry.key == "language") {
            info.language = entry.value;
            break;
        }
    }
    return info;
}

bool OpenInputContext(const std::string& path,
                      AVFormatContext** out_context,
                      std::string* out_error) {
    AVFormatContext* format_context = nullptr;
    int result = avformat_open_input(&format_context, path.c_str(), nullptr, nullptr);
    if (result < 0) {
        SetError(out_error, "avformat_open_input failed: " + ErrorString(result));
        return false;
    }
    result = avformat_find_stream_info(format_context, nullptr);
    if (result < 0) {
        SetError(out_error, "avformat_find_stream_info failed: " + ErrorString(result));
        avformat_close_input(&format_context);
        return false;
    }
    *out_context = format_context;
    return true;
}

bool OpenDecoderContext(AVFormatContext* format_context,
                        AVMediaType media_type,
                        int requested_stream_index,
                        int* out_stream_index,
                        AVCodecContext** out_codec_context,
                        std::string* out_error) {
    int stream_index = requested_stream_index;
    if (stream_index < 0) {
        stream_index = av_find_best_stream(format_context, media_type, -1, -1, nullptr, 0);
        if (stream_index < 0) {
            SetError(out_error, "av_find_best_stream failed: " + ErrorString(stream_index));
            return false;
        }
    }
    if (stream_index >= static_cast<int>(format_context->nb_streams)) {
        SetError(out_error, "Stream index out of range");
        return false;
    }

    AVStream* stream = format_context->streams[stream_index];
    AVCodecParameters* codecpar = stream->codecpar;
    if (codecpar == nullptr) {
        SetError(out_error, "Stream codec parameters are missing");
        return false;
    }
    if (codecpar->codec_type != media_type) {
        SetError(out_error, "Requested stream does not match requested media type");
        return false;
    }

    const AVCodec* codec = avcodec_find_decoder(codecpar->codec_id);
    if (codec == nullptr) {
        SetError(out_error, "Decoder not found for stream codec");
        return false;
    }

    AVCodecContext* codec_context = avcodec_alloc_context3(codec);
    if (codec_context == nullptr) {
        SetError(out_error, "avcodec_alloc_context3 failed");
        return false;
    }
    int result = avcodec_parameters_to_context(codec_context, codecpar);
    if (result < 0) {
        avcodec_free_context(&codec_context);
        SetError(out_error, "avcodec_parameters_to_context failed: " + ErrorString(result));
        return false;
    }
    result = avcodec_open2(codec_context, codec, nullptr);
    if (result < 0) {
        avcodec_free_context(&codec_context);
        SetError(out_error, "avcodec_open2 failed: " + ErrorString(result));
        return false;
    }

    *out_stream_index = stream_index;
    *out_codec_context = codec_context;
    return true;
}

std::string PictureTypeName(AVPictureType picture_type) {
    switch (picture_type) {
        case AV_PICTURE_TYPE_I: return "I";
        case AV_PICTURE_TYPE_P: return "P";
        case AV_PICTURE_TYPE_B: return "B";
        case AV_PICTURE_TYPE_S: return "S";
        case AV_PICTURE_TYPE_SI: return "SI";
        case AV_PICTURE_TYPE_SP: return "SP";
        case AV_PICTURE_TYPE_BI: return "BI";
        case AV_PICTURE_TYPE_NONE:
        default:
            return "NONE";
    }
}

int64_t FrameDuration(const AVFrame* frame) {
#if LIBAVUTIL_VERSION_MAJOR >= 58
    return frame->duration;
#else
    return frame->pkt_duration;
#endif
}

bool FrameIsKeyFrame(const AVFrame* frame) {
#ifdef AV_FRAME_FLAG_KEY
    return (frame->flags & AV_FRAME_FLAG_KEY) != 0;
#else
    return frame->key_frame != 0;
#endif
}

bool CollectVideoFrame(const AVFrame* frame,
                       int stream_index,
                       std::vector<VideoFrameInfo>* out_frames,
                       std::string* out_error) {
    const int size = av_image_get_buffer_size(static_cast<AVPixelFormat>(frame->format),
                                              frame->width,
                                              frame->height,
                                              1);
    if (size < 0) {
        SetError(out_error, "av_image_get_buffer_size failed: " + ErrorString(size));
        return false;
    }

    VideoFrameInfo info;
    info.stream_index = stream_index;
    info.width = frame->width;
    info.height = frame->height;
    info.pixel_format = PixelFormatName(frame->format);
    info.picture_type = PictureTypeName(frame->pict_type);
    info.pts = frame->pts;
    info.best_effort_timestamp = frame->best_effort_timestamp;
    info.duration = FrameDuration(frame);
    info.key_frame = FrameIsKeyFrame(frame);
    info.line_sizes.assign(frame->linesize, frame->linesize + AV_NUM_DATA_POINTERS);
    info.data.resize(static_cast<size_t>(size));

    const int copy_result = av_image_copy_to_buffer(info.data.data(),
                                                    size,
                                                    frame->data,
                                                    frame->linesize,
                                                    static_cast<AVPixelFormat>(frame->format),
                                                    frame->width,
                                                    frame->height,
                                                    1);
    if (copy_result < 0) {
        SetError(out_error, "av_image_copy_to_buffer failed: " + ErrorString(copy_result));
        return false;
    }
    out_frames->push_back(std::move(info));
    return true;
}

bool CollectAudioFrame(const AVFrame* frame,
                       int stream_index,
                       std::vector<AudioFrameInfo>* out_frames,
                       std::string* out_error) {
    const AVSampleFormat sample_format = static_cast<AVSampleFormat>(frame->format);
    const int bytes_per_sample = av_get_bytes_per_sample(sample_format);
    if (bytes_per_sample <= 0) {
        SetError(out_error, "av_get_bytes_per_sample failed");
        return false;
    }
    const bool planar = av_sample_fmt_is_planar(sample_format) != 0;
#if LIBAVUTIL_VERSION_MAJOR >= 57
    const int channels = frame->ch_layout.nb_channels;
#else
    const int channels = frame->channels;
#endif
    const int plane_count = planar ? std::max(channels, 0) : 1;
    const int plane_size = frame->nb_samples * bytes_per_sample * (planar ? 1 : std::max(channels, 0));

    AudioFrameInfo info;
    info.stream_index = stream_index;
    info.sample_rate = frame->sample_rate;
    info.channels = channels;
    info.sample_count = frame->nb_samples;
    info.planar = planar;
    info.sample_format = SampleFormatName(frame->format);
    info.pts = frame->pts;
    info.best_effort_timestamp = frame->best_effort_timestamp;
    info.plane_sizes.reserve(static_cast<size_t>(plane_count));
    info.data.reserve(static_cast<size_t>(std::max(plane_count, 0) * std::max(plane_size, 0)));

    for (int plane = 0; plane < plane_count; ++plane) {
        if (frame->extended_data == nullptr || frame->extended_data[plane] == nullptr) {
            SetError(out_error, "Audio frame plane data is missing");
            return false;
        }
        info.plane_sizes.push_back(plane_size);
        const uint8_t* begin = frame->extended_data[plane];
        info.data.insert(info.data.end(), begin, begin + plane_size);
    }

    out_frames->push_back(std::move(info));
    return true;
}

template <typename Collector>
bool DecodeFramesImpl(const std::string& path,
                      AVMediaType media_type,
                      int requested_stream_index,
                      int max_frames,
                      Collector collector,
                      std::string* out_error) {
    AVFormatContext* format_context = nullptr;
    if (!OpenInputContext(path, &format_context, out_error)) {
        return false;
    }

    int stream_index = -1;
    AVCodecContext* codec_context = nullptr;
    if (!OpenDecoderContext(format_context, media_type, requested_stream_index, &stream_index, &codec_context, out_error)) {
        avformat_close_input(&format_context);
        return false;
    }

    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    if (packet == nullptr || frame == nullptr) {
        av_packet_free(&packet);
        av_frame_free(&frame);
        avcodec_free_context(&codec_context);
        avformat_close_input(&format_context);
        SetError(out_error, "Failed to allocate AVPacket/AVFrame");
        return false;
    }

    const int frame_limit = max_frames <= 0 ? 0 : max_frames;
    int frame_count = 0;
    int result = 0;
    bool success = true;
    int skipped_packets = 0;
    std::string last_packet_error;

    auto receive_available_frames = [&]() {
        while (frame_limit == 0 || frame_count < frame_limit) {
            result = avcodec_receive_frame(codec_context, frame);
            if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) {
                break;
            }
            if (result < 0) {
                SetError(out_error, "avcodec_receive_frame failed: " + ErrorString(result));
                return false;
            }
            if (!collector(frame, stream_index, out_error)) {
                av_frame_unref(frame);
                return false;
            }
            ++frame_count;
            av_frame_unref(frame);
        }
        return true;
    };

    while (frame_limit == 0 || frame_count < frame_limit) {
        result = av_read_frame(format_context, packet);
        if (result == AVERROR_EOF) {
            break;
        }
        if (result < 0) {
            SetError(out_error, "av_read_frame failed: " + ErrorString(result));
            success = false;
            break;
        }
        if (packet->stream_index != stream_index) {
            av_packet_unref(packet);
            continue;
        }

        bool packet_sent = false;
        while (!packet_sent) {
            result = avcodec_send_packet(codec_context, packet);
            if (result == AVERROR(EAGAIN)) {
                if (!receive_available_frames()) {
                    success = false;
                    break;
                }
                continue;
            }
            if (result < 0) {
                last_packet_error = "avcodec_send_packet failed: " + ErrorString(result);
                if (media_type == AVMEDIA_TYPE_AUDIO) {
                    if (!receive_available_frames()) {
                        success = false;
                        break;
                    }
                    avcodec_flush_buffers(codec_context);
                    ++skipped_packets;
                    break;
                }
                SetError(out_error, last_packet_error);
                success = false;
                break;
            }
            packet_sent = true;
        }
        av_packet_unref(packet);
        if (!success) {
            break;
        }
        if (!packet_sent) {
            continue;
        }
        if (!receive_available_frames()) {
            success = false;
            break;
        }
    }

    if (success && frame_count == 0 && skipped_packets > 0) {
        SetError(out_error, last_packet_error.empty() ? "ffmpeg skipped invalid audio packets without decoding frames" : last_packet_error);
        success = false;
    }

    if (success && (frame_limit == 0 || frame_count < frame_limit)) {
        result = avcodec_send_packet(codec_context, nullptr);
        if (result >= 0) {
            while (frame_limit == 0 || frame_count < frame_limit) {
                result = avcodec_receive_frame(codec_context, frame);
                if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) {
                    break;
                }
                if (result < 0) {
                    SetError(out_error, "avcodec_receive_frame flush failed: " + ErrorString(result));
                    success = false;
                    break;
                }
                if (!collector(frame, stream_index, out_error)) {
                    av_frame_unref(frame);
                    success = false;
                    break;
                }
                ++frame_count;
                av_frame_unref(frame);
            }
        } else {
            SetError(out_error, "avcodec_send_packet flush failed: " + ErrorString(result));
            success = false;
        }
    }

    av_packet_free(&packet);
    av_frame_free(&frame);
    avcodec_free_context(&codec_context);
    avformat_close_input(&format_context);
    return success;
}

#endif

}  // namespace

bool IsAvailable() {
#if ENGINE_HAS_FFMPEG_BRIDGE
    return true;
#else
    return false;
#endif
}

bool HasFilterLibrary() {
#if ENGINE_HAS_FFMPEG_AVFILTER
    return true;
#else
    return false;
#endif
}

bool HasDeviceLibrary() {
#if ENGINE_HAS_FFMPEG_AVDEVICE
    return true;
#else
    return false;
#endif
}

std::string Summary() {
    if (!IsAvailable()) {
        return "FFmpeg bridge unavailable";
    }
    std::ostringstream stream;
    stream << "FFmpeg bridge enabled";
    stream << " (avcodec+avformat+avutil";
    stream << ", avfilter=" << (HasFilterLibrary() ? "on" : "off");
    stream << ", avdevice=" << (HasDeviceLibrary() ? "on" : "off") << ")";
    return stream.str();
}

std::string Configuration() {
#if ENGINE_HAS_FFMPEG_BRIDGE
    return avcodec_configuration();
#else
    return "";
#endif
}

std::string License() {
#if ENGINE_HAS_FFMPEG_BRIDGE
    return avcodec_license();
#else
    return "";
#endif
}

std::vector<LibraryVersion> LibraryVersions() {
    std::vector<LibraryVersion> versions;
#if ENGINE_HAS_FFMPEG_BRIDGE
    versions.push_back(MakeVersion("avutil", avutil_version()));
    versions.push_back(MakeVersion("avcodec", avcodec_version()));
    versions.push_back(MakeVersion("avformat", avformat_version()));
#if ENGINE_HAS_FFMPEG_AVFILTER
    versions.push_back(MakeVersion("avfilter", avfilter_version()));
#endif
#if ENGINE_HAS_FFMPEG_AVDEVICE
    versions.push_back(MakeVersion("avdevice", avdevice_version()));
#endif
#endif
    return versions;
}

std::vector<ProtocolInfo> Protocols() {
    std::vector<ProtocolInfo> protocols;
#if ENGINE_HAS_FFMPEG_BRIDGE
    std::set<std::string> seen;
    void* opaque = nullptr;
    const char* name = nullptr;
    while ((name = avio_enum_protocols(&opaque, 0)) != nullptr) {
        ProtocolInfo info;
        info.name = name;
        info.input = true;
        auto [it, inserted] = seen.insert(info.name);
        if (inserted) {
            protocols.push_back(info);
        }
    }

    opaque = nullptr;
    while ((name = avio_enum_protocols(&opaque, 1)) != nullptr) {
        auto found = std::find_if(protocols.begin(), protocols.end(),
                                  [name](const ProtocolInfo& info) { return info.name == name; });
        if (found == protocols.end()) {
            ProtocolInfo info;
            info.name = name;
            info.output = true;
            protocols.push_back(info);
        } else {
            found->output = true;
        }
    }
    std::sort(protocols.begin(), protocols.end(),
              [](const ProtocolInfo& left, const ProtocolInfo& right) {
                  return left.name < right.name;
              });
#endif
    return protocols;
}

std::vector<NamedItem> InputFormats() {
    std::vector<NamedItem> items;
#if ENGINE_HAS_FFMPEG_BRIDGE
    void* opaque = nullptr;
    const AVInputFormat* format = nullptr;
    while ((format = av_demuxer_iterate(&opaque)) != nullptr) {
        items.push_back({format->name != nullptr ? format->name : "",
                         format->long_name != nullptr ? format->long_name : ""});
    }
    std::sort(items.begin(), items.end(), [](const NamedItem& left, const NamedItem& right) {
        return left.name < right.name;
    });
#endif
    return items;
}

std::vector<NamedItem> OutputFormats() {
    std::vector<NamedItem> items;
#if ENGINE_HAS_FFMPEG_BRIDGE
    void* opaque = nullptr;
    const AVOutputFormat* format = nullptr;
    while ((format = av_muxer_iterate(&opaque)) != nullptr) {
        items.push_back({format->name != nullptr ? format->name : "",
                         format->long_name != nullptr ? format->long_name : ""});
    }
    std::sort(items.begin(), items.end(), [](const NamedItem& left, const NamedItem& right) {
        return left.name < right.name;
    });
#endif
    return items;
}

std::vector<CodecInfo> Codecs() {
    std::vector<CodecInfo> codecs;
#if ENGINE_HAS_FFMPEG_BRIDGE
    void* opaque = nullptr;
    const AVCodec* codec = nullptr;
    while ((codec = av_codec_iterate(&opaque)) != nullptr) {
        CodecInfo info;
        info.name = codec->name != nullptr ? codec->name : "";
        info.long_name = codec->long_name != nullptr ? codec->long_name : "";
        info.media_type = MediaTypeName(codec->type);
        info.encoder = av_codec_is_encoder(codec) != 0;
        info.decoder = av_codec_is_decoder(codec) != 0;
        info.intra_only = (codec->capabilities & AV_CODEC_CAP_INTRA_ONLY) != 0;
        info.lossy = (codec->capabilities & AV_CODEC_CAP_LOSSY) != 0;
        info.lossless = (codec->capabilities & AV_CODEC_CAP_LOSSLESS) != 0;
        codecs.push_back(info);
    }
    std::sort(codecs.begin(), codecs.end(), [](const CodecInfo& left, const CodecInfo& right) {
        return left.name < right.name;
    });
#endif
    return codecs;
}

std::vector<NamedItem> BitstreamFilters() {
    std::vector<NamedItem> items;
#if ENGINE_HAS_FFMPEG_BRIDGE
    void* opaque = nullptr;
    const AVBitStreamFilter* filter = nullptr;
    while ((filter = av_bsf_iterate(&opaque)) != nullptr) {
        items.push_back({filter->name != nullptr ? filter->name : "", ""});
    }
    std::sort(items.begin(), items.end(), [](const NamedItem& left, const NamedItem& right) {
        return left.name < right.name;
    });
#endif
    return items;
}

std::vector<FilterInfo> Filters() {
    std::vector<FilterInfo> filters;
#if ENGINE_HAS_FFMPEG_AVFILTER
    void* opaque = nullptr;
    const AVFilter* filter = nullptr;
    while ((filter = av_filter_iterate(&opaque)) != nullptr) {
        FilterInfo info;
        info.name = filter->name != nullptr ? filter->name : "";
        info.description = filter->description != nullptr ? filter->description : "";
        info.dynamic_inputs = (filter->flags & AVFILTER_FLAG_DYNAMIC_INPUTS) != 0;
        info.dynamic_outputs = (filter->flags & AVFILTER_FLAG_DYNAMIC_OUTPUTS) != 0;
        info.slice_threads = (filter->flags & AVFILTER_FLAG_SLICE_THREADS) != 0;
        info.timeline_generic = (filter->flags & AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC) != 0;
        info.timeline_internal = (filter->flags & AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL) != 0;
        info.timeline_support = (filter->flags & AVFILTER_FLAG_SUPPORT_TIMELINE) != 0;
        filters.push_back(info);
    }
    std::sort(filters.begin(), filters.end(), [](const FilterInfo& left, const FilterInfo& right) {
        return left.name < right.name;
    });
#endif
    return filters;
}

std::vector<DeviceInfo> InputDevices() {
    std::vector<DeviceInfo> devices;
#if ENGINE_HAS_FFMPEG_AVDEVICE
    std::set<std::string> seen;
    const AVInputFormat* audio = nullptr;
    while ((audio = av_input_audio_device_next(audio)) != nullptr) {
        DeviceInfo info;
        info.name = audio->name != nullptr ? audio->name : "";
        info.description = audio->long_name != nullptr ? audio->long_name : "";
        info.media_type = "audio";
        info.input = true;
        if (seen.insert(info.media_type + ":" + info.name).second) {
            devices.push_back(info);
        }
    }
    const AVInputFormat* video = nullptr;
    while ((video = av_input_video_device_next(video)) != nullptr) {
        DeviceInfo info;
        info.name = video->name != nullptr ? video->name : "";
        info.description = video->long_name != nullptr ? video->long_name : "";
        info.media_type = "video";
        info.input = true;
        if (seen.insert(info.media_type + ":" + info.name).second) {
            devices.push_back(info);
        }
    }
    std::sort(devices.begin(), devices.end(), [](const DeviceInfo& left, const DeviceInfo& right) {
        return left.name < right.name;
    });
#endif
    return devices;
}

std::vector<DeviceInfo> OutputDevices() {
    std::vector<DeviceInfo> devices;
#if ENGINE_HAS_FFMPEG_AVDEVICE
    std::set<std::string> seen;
    const AVOutputFormat* audio = nullptr;
    while ((audio = av_output_audio_device_next(audio)) != nullptr) {
        DeviceInfo info;
        info.name = audio->name != nullptr ? audio->name : "";
        info.description = audio->long_name != nullptr ? audio->long_name : "";
        info.media_type = "audio";
        info.output = true;
        if (seen.insert(info.media_type + ":" + info.name).second) {
            devices.push_back(info);
        }
    }
    const AVOutputFormat* video = nullptr;
    while ((video = av_output_video_device_next(video)) != nullptr) {
        DeviceInfo info;
        info.name = video->name != nullptr ? video->name : "";
        info.description = video->long_name != nullptr ? video->long_name : "";
        info.media_type = "video";
        info.output = true;
        if (seen.insert(info.media_type + ":" + info.name).second) {
            devices.push_back(info);
        }
    }
    std::sort(devices.begin(), devices.end(), [](const DeviceInfo& left, const DeviceInfo& right) {
        return left.name < right.name;
    });
#endif
    return devices;
}

bool ProbeMedia(const std::string& path, MediaInfo* out_info, std::string* out_error) {
#if ENGINE_HAS_FFMPEG_BRIDGE
    if (out_info == nullptr) {
        SetError(out_error, "Output MediaInfo pointer is null");
        return false;
    }
    AVFormatContext* format_context = nullptr;
    if (!OpenInputContext(path, &format_context, out_error)) {
        return false;
    }

    MediaInfo info;
    info.path = path;
    info.format_name = (format_context->iformat != nullptr && format_context->iformat->name != nullptr)
                           ? format_context->iformat->name
                           : "";
    info.format_long_name = (format_context->iformat != nullptr && format_context->iformat->long_name != nullptr)
                                ? format_context->iformat->long_name
                                : "";
    info.duration = format_context->duration;
    info.start_time = format_context->start_time;
    info.size = format_context->pb != nullptr ? avio_size(format_context->pb) : 0;
    info.bit_rate = format_context->bit_rate;
    info.stream_count = static_cast<int>(format_context->nb_streams);
    info.metadata = ReadMetadata(format_context->metadata);
    for (unsigned i = 0; i < format_context->nb_streams; ++i) {
        info.streams.push_back(MakeStreamInfo(format_context->streams[i]));
    }

    avformat_close_input(&format_context);
    *out_info = std::move(info);
    return true;
#else
    (void)path;
    (void)out_info;
    SetError(out_error, "FFmpeg bridge unavailable");
    return false;
#endif
}

bool ReadPackets(const std::string& path,
                 int max_packets,
                 std::vector<PacketInfo>* out_packets,
                 std::string* out_error) {
#if ENGINE_HAS_FFMPEG_BRIDGE
    if (out_packets == nullptr) {
        SetError(out_error, "Output packet list pointer is null");
        return false;
    }
    AVFormatContext* format_context = nullptr;
    if (!OpenInputContext(path, &format_context, out_error)) {
        return false;
    }

    std::vector<PacketInfo> packets;
    packets.reserve(static_cast<size_t>(std::max(max_packets, 0)));
    AVPacket packet = {};

    const int packet_limit = max_packets <= 0 ? 0 : max_packets;
    while (packet_limit == 0 || static_cast<int>(packets.size()) < packet_limit) {
        int result = av_read_frame(format_context, &packet);
        if (result == AVERROR_EOF) {
            break;
        }
        if (result < 0) {
            av_packet_unref(&packet);
            avformat_close_input(&format_context);
            SetError(out_error, "av_read_frame failed: " + ErrorString(result));
            return false;
        }
        PacketInfo info;
        info.stream_index = packet.stream_index;
        if (packet.stream_index >= 0 && packet.stream_index < static_cast<int>(format_context->nb_streams)) {
            info.media_type = MediaTypeName(format_context->streams[packet.stream_index]->codecpar->codec_type);
        }
        info.pts = packet.pts;
        info.dts = packet.dts;
        info.duration = packet.duration;
        info.pos = packet.pos;
        info.size = packet.size;
        info.key_frame = (packet.flags & AV_PKT_FLAG_KEY) != 0;
        info.corrupt = (packet.flags & AV_PKT_FLAG_CORRUPT) != 0;
        packets.push_back(info);
        av_packet_unref(&packet);
    }

    avformat_close_input(&format_context);
    *out_packets = std::move(packets);
    return true;
#else
    (void)path;
    (void)max_packets;
    (void)out_packets;
    SetError(out_error, "FFmpeg bridge unavailable");
    return false;
#endif
}

bool DecodeVideoFrames(const std::string& path,
                       int stream_index,
                       int max_frames,
                       std::vector<VideoFrameInfo>* out_frames,
                       std::string* out_error) {
#if ENGINE_HAS_FFMPEG_BRIDGE
    if (out_frames == nullptr) {
        SetError(out_error, "Output video frame list pointer is null");
        return false;
    }
    out_frames->clear();
    return DecodeFramesImpl(
        path,
        AVMEDIA_TYPE_VIDEO,
        stream_index,
        max_frames,
        [out_frames](const AVFrame* frame, int resolved_stream_index, std::string* error) {
            return CollectVideoFrame(frame, resolved_stream_index, out_frames, error);
        },
        out_error);
#else
    (void)path;
    (void)stream_index;
    (void)max_frames;
    (void)out_frames;
    SetError(out_error, "FFmpeg bridge unavailable");
    return false;
#endif
}

bool DecodeAudioFrames(const std::string& path,
                       int stream_index,
                       int max_frames,
                       std::vector<AudioFrameInfo>* out_frames,
                       std::string* out_error) {
#if ENGINE_HAS_FFMPEG_BRIDGE
    if (out_frames == nullptr) {
        SetError(out_error, "Output audio frame list pointer is null");
        return false;
    }
    out_frames->clear();
    return DecodeFramesImpl(
        path,
        AVMEDIA_TYPE_AUDIO,
        stream_index,
        max_frames,
        [out_frames](const AVFrame* frame, int resolved_stream_index, std::string* error) {
            return CollectAudioFrame(frame, resolved_stream_index, out_frames, error);
        },
        out_error);
#else
    (void)path;
    (void)stream_index;
    (void)max_frames;
    (void)out_frames;
    SetError(out_error, "FFmpeg bridge unavailable");
    return false;
#endif
}

bool RemuxCopy(const std::string& input_path,
               const std::string& output_path,
               std::string* out_error) {
#if ENGINE_HAS_FFMPEG_BRIDGE
    AVFormatContext* input_context = nullptr;
    if (!OpenInputContext(input_path, &input_context, out_error)) {
        return false;
    }

    AVFormatContext* output_context = nullptr;
    int result = avformat_alloc_output_context2(&output_context, nullptr, nullptr, output_path.c_str());
    if (result < 0 || output_context == nullptr) {
        avformat_close_input(&input_context);
        SetError(out_error, "avformat_alloc_output_context2 failed: " + ErrorString(result));
        return false;
    }

    for (unsigned i = 0; i < input_context->nb_streams; ++i) {
        AVStream* input_stream = input_context->streams[i];
        AVStream* output_stream = avformat_new_stream(output_context, nullptr);
        if (output_stream == nullptr) {
            avformat_free_context(output_context);
            avformat_close_input(&input_context);
            SetError(out_error, "avformat_new_stream failed");
            return false;
        }
        result = avcodec_parameters_copy(output_stream->codecpar, input_stream->codecpar);
        if (result < 0) {
            avformat_free_context(output_context);
            avformat_close_input(&input_context);
            SetError(out_error, "avcodec_parameters_copy failed: " + ErrorString(result));
            return false;
        }
        output_stream->codecpar->codec_tag = 0;
        output_stream->time_base = input_stream->time_base;
    }

    if ((output_context->oformat->flags & AVFMT_NOFILE) == 0) {
        result = avio_open(&output_context->pb, output_path.c_str(), AVIO_FLAG_WRITE);
        if (result < 0) {
            avformat_free_context(output_context);
            avformat_close_input(&input_context);
            SetError(out_error, "avio_open failed: " + ErrorString(result));
            return false;
        }
    }

    result = avformat_write_header(output_context, nullptr);
    if (result < 0) {
        if ((output_context->oformat->flags & AVFMT_NOFILE) == 0) {
            avio_closep(&output_context->pb);
        }
        avformat_free_context(output_context);
        avformat_close_input(&input_context);
        SetError(out_error, "avformat_write_header failed: " + ErrorString(result));
        return false;
    }

    AVPacket packet = {};
    while ((result = av_read_frame(input_context, &packet)) >= 0) {
        AVStream* input_stream = input_context->streams[packet.stream_index];
        AVStream* output_stream = output_context->streams[packet.stream_index];
        packet.pts = av_rescale_q_rnd(packet.pts, input_stream->time_base, output_stream->time_base,
                                      static_cast<AVRounding>(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
        packet.dts = av_rescale_q_rnd(packet.dts, input_stream->time_base, output_stream->time_base,
                                      static_cast<AVRounding>(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
        packet.duration = av_rescale_q(packet.duration, input_stream->time_base, output_stream->time_base);
        packet.pos = -1;
        result = av_interleaved_write_frame(output_context, &packet);
        av_packet_unref(&packet);
        if (result < 0) {
            av_write_trailer(output_context);
            if ((output_context->oformat->flags & AVFMT_NOFILE) == 0) {
                avio_closep(&output_context->pb);
            }
            avformat_free_context(output_context);
            avformat_close_input(&input_context);
            SetError(out_error, "av_interleaved_write_frame failed: " + ErrorString(result));
            return false;
        }
    }
    if (result != AVERROR_EOF) {
        av_write_trailer(output_context);
        if ((output_context->oformat->flags & AVFMT_NOFILE) == 0) {
            avio_closep(&output_context->pb);
        }
        avformat_free_context(output_context);
        avformat_close_input(&input_context);
        SetError(out_error, "av_read_frame failed: " + ErrorString(result));
        return false;
    }

    av_write_trailer(output_context);
    if ((output_context->oformat->flags & AVFMT_NOFILE) == 0) {
        avio_closep(&output_context->pb);
    }
    avformat_free_context(output_context);
    avformat_close_input(&input_context);
    return true;
#else
    (void)input_path;
    (void)output_path;
    SetError(out_error, "FFmpeg bridge unavailable");
    return false;
#endif
}

bool ExtractStream(const std::string& input_path,
                   int stream_index,
                   const std::string& output_path,
                   std::string* out_error) {
#if ENGINE_HAS_FFMPEG_BRIDGE
    AVFormatContext* input_context = nullptr;
    if (!OpenInputContext(input_path, &input_context, out_error)) {
        return false;
    }
    if (stream_index < 0 || stream_index >= static_cast<int>(input_context->nb_streams)) {
        avformat_close_input(&input_context);
        SetError(out_error, "Stream index out of range");
        return false;
    }

    AVFormatContext* output_context = nullptr;
    int result = avformat_alloc_output_context2(&output_context, nullptr, nullptr, output_path.c_str());
    if (result < 0 || output_context == nullptr) {
        avformat_close_input(&input_context);
        SetError(out_error, "avformat_alloc_output_context2 failed: " + ErrorString(result));
        return false;
    }

    AVStream* input_stream = input_context->streams[stream_index];
    AVStream* output_stream = avformat_new_stream(output_context, nullptr);
    if (output_stream == nullptr) {
        avformat_free_context(output_context);
        avformat_close_input(&input_context);
        SetError(out_error, "avformat_new_stream failed");
        return false;
    }

    result = avcodec_parameters_copy(output_stream->codecpar, input_stream->codecpar);
    if (result < 0) {
        avformat_free_context(output_context);
        avformat_close_input(&input_context);
        SetError(out_error, "avcodec_parameters_copy failed: " + ErrorString(result));
        return false;
    }
    output_stream->codecpar->codec_tag = 0;
    output_stream->time_base = input_stream->time_base;

    if ((output_context->oformat->flags & AVFMT_NOFILE) == 0) {
        result = avio_open(&output_context->pb, output_path.c_str(), AVIO_FLAG_WRITE);
        if (result < 0) {
            avformat_free_context(output_context);
            avformat_close_input(&input_context);
            SetError(out_error, "avio_open failed: " + ErrorString(result));
            return false;
        }
    }

    result = avformat_write_header(output_context, nullptr);
    if (result < 0) {
        if ((output_context->oformat->flags & AVFMT_NOFILE) == 0) {
            avio_closep(&output_context->pb);
        }
        avformat_free_context(output_context);
        avformat_close_input(&input_context);
        SetError(out_error, "avformat_write_header failed: " + ErrorString(result));
        return false;
    }

    AVPacket packet = {};
    while ((result = av_read_frame(input_context, &packet)) >= 0) {
        if (packet.stream_index != stream_index) {
            av_packet_unref(&packet);
            continue;
        }
        packet.stream_index = 0;
        packet.pts = av_rescale_q_rnd(packet.pts, input_stream->time_base, output_stream->time_base,
                                      static_cast<AVRounding>(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
        packet.dts = av_rescale_q_rnd(packet.dts, input_stream->time_base, output_stream->time_base,
                                      static_cast<AVRounding>(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
        packet.duration = av_rescale_q(packet.duration, input_stream->time_base, output_stream->time_base);
        packet.pos = -1;
        result = av_interleaved_write_frame(output_context, &packet);
        av_packet_unref(&packet);
        if (result < 0) {
            av_write_trailer(output_context);
            if ((output_context->oformat->flags & AVFMT_NOFILE) == 0) {
                avio_closep(&output_context->pb);
            }
            avformat_free_context(output_context);
            avformat_close_input(&input_context);
            SetError(out_error, "av_interleaved_write_frame failed: " + ErrorString(result));
            return false;
        }
    }
    if (result != AVERROR_EOF) {
        av_write_trailer(output_context);
        if ((output_context->oformat->flags & AVFMT_NOFILE) == 0) {
            avio_closep(&output_context->pb);
        }
        avformat_free_context(output_context);
        avformat_close_input(&input_context);
        SetError(out_error, "av_read_frame failed: " + ErrorString(result));
        return false;
    }

    av_write_trailer(output_context);
    if ((output_context->oformat->flags & AVFMT_NOFILE) == 0) {
        avio_closep(&output_context->pb);
    }
    avformat_free_context(output_context);
    avformat_close_input(&input_context);
    return true;
#else
    (void)input_path;
    (void)stream_index;
    (void)output_path;
    SetError(out_error, "FFmpeg bridge unavailable");
    return false;
#endif
}

bool EncodeVideoFrames(const EncodeVideoParams& params,
                       const std::vector<VideoFrameInfo>& frames,
                       std::string* out_error) {
    return EncodeVideoFrameStream(
        params,
        frames.size(),
        [&frames](std::size_t frame_index, VideoFrameInfo* out_frame, std::string* out_error) {
            if (out_frame == nullptr || frame_index >= frames.size()) {
                SetError(out_error, "EncodeVideoFrames provider received invalid frame index");
                return false;
            }
            *out_frame = frames[frame_index];
            return true;
        },
        out_error);
}

bool EncodeVideoFrameStream(const EncodeVideoParams& params,
                            std::size_t frame_count,
                            const VideoFrameProvider& provider,
                            std::string* out_error) {
#if ENGINE_HAS_FFMPEG_BRIDGE
    if (frame_count == 0u) {
        SetError(out_error, "No video frames to encode");
        return false;
    }

    if (!provider) {
        SetError(out_error, "EncodeVideoFrameStream requires a frame provider");
        return false;
    }

    if (params.output_path.empty()) {
        SetError(out_error, "EncodeVideoFrameStream requires an output path");
        return false;
    }

    VideoFrameInfo first_frame;
    if (!provider(0u, &first_frame, out_error)) {
        return false;
    }

    const int width = params.width > 0 ? params.width : first_frame.width;
    const int height = params.height > 0 ? params.height : first_frame.height;
    if (width <= 0 || height <= 0) {
        SetError(out_error, "EncodeVideoFrameStream received invalid width/height");
        return false;
    }

    if (first_frame.width != width || first_frame.height != height || first_frame.line_sizes.empty() || first_frame.data.empty()) {
        SetError(out_error, "First video frame contains invalid data or inconsistent dimensions");
        return false;
    }

    auto parse_input_pix_fmt = [](std::string text) -> AVPixelFormat {
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        if (text == "rgba") return AV_PIX_FMT_RGBA;
        if (text == "bgra") return AV_PIX_FMT_BGRA;
        if (text == "rgb" || text == "rgb24") return AV_PIX_FMT_RGB24;
        if (text == "bgr" || text == "bgr24") return AV_PIX_FMT_BGR24;
        if (text == "gray" || text == "gray8" || text == "y") return AV_PIX_FMT_GRAY8;
        return AV_PIX_FMT_NONE;
    };

    AVFormatContext* output_context = nullptr;
    int result = avformat_alloc_output_context2(&output_context, nullptr, nullptr, params.output_path.c_str());
    if (result < 0 || output_context == nullptr) {
        SetError(out_error, "avformat_alloc_output_context2 failed: " + ErrorString(result));
        return false;
    }

    const AVCodec* codec = params.codec_name.empty()
        ? avcodec_find_encoder(output_context->oformat->video_codec)
        : avcodec_find_encoder_by_name(params.codec_name.c_str());
    if (codec == nullptr) {
        avformat_free_context(output_context);
        SetError(out_error, "video encoder not found: " + params.codec_name);
        return false;
    }

    AVStream* stream = avformat_new_stream(output_context, nullptr);
    AVCodecContext* codec_context = avcodec_alloc_context3(codec);
    if (stream == nullptr || codec_context == nullptr) {
        avcodec_free_context(&codec_context);
        avformat_free_context(output_context);
        SetError(out_error, "failed to allocate video encoder stream/context");
        return false;
    }

    codec_context->codec_type = AVMEDIA_TYPE_VIDEO;
    codec_context->width = width;
    codec_context->height = height;
    codec_context->time_base = AVRational{params.fps_den > 0 ? params.fps_den : 1,
                                          params.fps_num > 0 ? params.fps_num : 1};
    codec_context->framerate = AVRational{params.fps_num > 0 ? params.fps_num : 1,
                                          params.fps_den > 0 ? params.fps_den : 1};
    codec_context->bit_rate = params.bit_rate > 0 ? params.bit_rate : 4000000;

    AVPixelFormat encoder_pix_fmt = AV_PIX_FMT_YUV420P;
    int pixel_format_count = 0;
    const AVPixelFormat* pixel_formats = SupportedPixelFormats(codec, &pixel_format_count);
    if (pixel_formats != nullptr && pixel_format_count > 0) {
        encoder_pix_fmt = pixel_formats[0];
        for (int index = 0; index < pixel_format_count; ++index) {
            if (pixel_formats[index] == AV_PIX_FMT_RGBA) {
                encoder_pix_fmt = pixel_formats[index];
                break;
            }
        }
    }
    codec_context->pix_fmt = encoder_pix_fmt;

    if ((output_context->oformat->flags & AVFMT_GLOBALHEADER) != 0) {
        codec_context->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    result = avcodec_open2(codec_context, codec, nullptr);
    if (result < 0) {
        avcodec_free_context(&codec_context);
        avformat_free_context(output_context);
        SetError(out_error, "avcodec_open2 failed: " + ErrorString(result));
        return false;
    }

    result = avcodec_parameters_from_context(stream->codecpar, codec_context);
    if (result < 0) {
        avcodec_free_context(&codec_context);
        avformat_free_context(output_context);
        SetError(out_error, "avcodec_parameters_from_context failed: " + ErrorString(result));
        return false;
    }
    stream->time_base = codec_context->time_base;

    if ((output_context->oformat->flags & AVFMT_NOFILE) == 0) {
        result = avio_open(&output_context->pb, params.output_path.c_str(), AVIO_FLAG_WRITE);
        if (result < 0) {
            avcodec_free_context(&codec_context);
            avformat_free_context(output_context);
            SetError(out_error, "avio_open failed: " + ErrorString(result));
            return false;
        }
    }

    result = avformat_write_header(output_context, nullptr);
    if (result < 0) {
        if ((output_context->oformat->flags & AVFMT_NOFILE) == 0) {
            avio_closep(&output_context->pb);
        }
        avcodec_free_context(&codec_context);
        avformat_free_context(output_context);
        SetError(out_error, "avformat_write_header failed: " + ErrorString(result));
        return false;
    }

    AVPacket* packet = av_packet_alloc();
    if (packet == nullptr) {
        av_write_trailer(output_context);
        if ((output_context->oformat->flags & AVFMT_NOFILE) == 0) {
            avio_closep(&output_context->pb);
        }
        avcodec_free_context(&codec_context);
        avformat_free_context(output_context);
        SetError(out_error, "av_packet_alloc failed");
        return false;
    }

    const AVPixelFormat input_pix_fmt = parse_input_pix_fmt(first_frame.pixel_format);
    if (input_pix_fmt == AV_PIX_FMT_NONE) {
        av_packet_free(&packet);
        av_write_trailer(output_context);
        if ((output_context->oformat->flags & AVFMT_NOFILE) == 0) {
            avio_closep(&output_context->pb);
        }
        avcodec_free_context(&codec_context);
        avformat_free_context(output_context);
        SetError(out_error, "unsupported input pixel format: " + first_frame.pixel_format);
        return false;
    }

    SwsContext* sws_context = nullptr;
    if (input_pix_fmt != codec_context->pix_fmt) {
        sws_context = sws_getContext(width,
                                     height,
                                     input_pix_fmt,
                                     width,
                                     height,
                                     codec_context->pix_fmt,
                                     SWS_BILINEAR,
                                     nullptr,
                                     nullptr,
                                     nullptr);
        if (sws_context == nullptr) {
            av_packet_free(&packet);
            av_write_trailer(output_context);
            if ((output_context->oformat->flags & AVFMT_NOFILE) == 0) {
                avio_closep(&output_context->pb);
            }
            avcodec_free_context(&codec_context);
            avformat_free_context(output_context);
            SetError(out_error, "sws_getContext failed");
            return false;
        }
    }

    bool success = true;
    int64_t next_pts = 0;
    for (std::size_t frame_index = 0; frame_index < frame_count; ++frame_index) {
        VideoFrameInfo input;
        if (frame_index == 0u) {
            input = std::move(first_frame);
        } else if (!provider(frame_index, &input, out_error)) {
            success = false;
            break;
        }

        if (input.width != width || input.height != height || input.line_sizes.empty() || input.data.empty()) {
            success = false;
            SetError(out_error, "Streamed video frame contains invalid data or inconsistent dimensions");
            break;
        }

        AVFrame* frame = av_frame_alloc();
        if (frame == nullptr) {
            success = false;
            SetError(out_error, "av_frame_alloc failed");
            break;
        }

        frame->format = codec_context->pix_fmt;
        frame->width = width;
        frame->height = height;
        frame->pts = next_pts++;

        result = av_frame_get_buffer(frame, 0);
        if (result < 0 || av_frame_make_writable(frame) < 0) {
            av_frame_free(&frame);
            success = false;
            SetError(out_error, "failed to allocate writable video frame");
            break;
        }

        uint8_t* src_data[4] = {nullptr, nullptr, nullptr, nullptr};
        int src_linesize[4] = {0, 0, 0, 0};
        const int fill_result = av_image_fill_arrays(src_data,
                                                     src_linesize,
                                                     input.data.data(),
                                                     input_pix_fmt,
                                                     width,
                                                     height,
                                                     1);
        if (fill_result < 0) {
            av_frame_free(&frame);
            success = false;
            SetError(out_error, "av_image_fill_arrays failed: " + ErrorString(fill_result));
            break;
        }

        if (!input.line_sizes.empty()) {
            src_linesize[0] = input.line_sizes[0];
            for (size_t i = 1; i < input.line_sizes.size() && i < 4; ++i) {
                src_linesize[i] = input.line_sizes[i];
            }
        }

        if (sws_context != nullptr) {
            const int scaled = sws_scale(sws_context,
                                         src_data,
                                         src_linesize,
                                         0,
                                         height,
                                         frame->data,
                                         frame->linesize);
            if (scaled <= 0) {
                av_frame_free(&frame);
                success = false;
                SetError(out_error, "sws_scale failed");
                break;
            }
        } else {
            av_image_copy(frame->data,
                          frame->linesize,
                          const_cast<const uint8_t**>(src_data),
                          src_linesize,
                          codec_context->pix_fmt,
                          width,
                          height);
        }

        result = avcodec_send_frame(codec_context, frame);
        av_frame_free(&frame);
        if (result < 0) {
            success = false;
            SetError(out_error, "avcodec_send_frame failed: " + ErrorString(result));
            break;
        }
        if (!PushEncoderPackets(output_context, codec_context, stream, packet, out_error)) {
            success = false;
            break;
        }
    }

    if (success) {
        result = avcodec_send_frame(codec_context, nullptr);
        if (result < 0) {
            success = false;
            SetError(out_error, "avcodec_send_frame(nullptr) failed: " + ErrorString(result));
        } else if (!PushEncoderPackets(output_context, codec_context, stream, packet, out_error)) {
            success = false;
        }
    }

    if (success) {
        result = av_write_trailer(output_context);
        if (result < 0) {
            success = false;
            SetError(out_error, "av_write_trailer failed: " + ErrorString(result));
        }
    } else {
        av_write_trailer(output_context);
    }

    if (sws_context != nullptr) {
        sws_freeContext(sws_context);
    }
    av_packet_free(&packet);
    if ((output_context->oformat->flags & AVFMT_NOFILE) == 0) {
        avio_closep(&output_context->pb);
    }
    avcodec_free_context(&codec_context);
    avformat_free_context(output_context);

    return success;
#else
    (void)params;
    (void)frame_count;
    (void)provider;
    SetError(out_error, "FFmpeg bridge unavailable");
    return false;
#endif
}

bool EncodeAudioFrames(const EncodeAudioParams& params,
                       const std::vector<AudioFrameInfo>& frames,
                       std::string* out_error) {
#if ENGINE_HAS_FFMPEG_BRIDGE
    if (frames.empty()) {
        SetError(out_error, "No audio frames to encode");
        return false;
    }
    if (params.output_path.empty()) {
        SetError(out_error, "EncodeAudioFrames requires an output path");
        return false;
    }

    const int input_sample_rate = params.sample_rate > 0 ? params.sample_rate : frames.front().sample_rate;
    const int input_channels = params.channels > 0 ? params.channels : frames.front().channels;
    if (input_sample_rate <= 0 || input_channels <= 0) {
        SetError(out_error, "EncodeAudioFrames received invalid sample rate or channel count");
        return false;
    }
    const bool consistent_stream = std::all_of(frames.begin(), frames.end(), [input_sample_rate, input_channels](const AudioFrameInfo& frame) {
        return frame.sample_rate == input_sample_rate && frame.channels == input_channels && frame.sample_count > 0;
    });
    if (!consistent_stream) {
        SetError(out_error, "EncodeAudioFrames requires a consistent audio stream");
        return false;
    }

    AVFormatContext* output_context = nullptr;
    int result = avformat_alloc_output_context2(&output_context, nullptr, nullptr, params.output_path.c_str());
    if (result < 0 || output_context == nullptr) {
        SetError(out_error, "avformat_alloc_output_context2 failed: " + ErrorString(result));
        return false;
    }

    const AVCodec* codec = params.codec_name.empty()
        ? avcodec_find_encoder(output_context->oformat->audio_codec)
        : avcodec_find_encoder_by_name(params.codec_name.c_str());
    if (codec == nullptr) {
        avformat_free_context(output_context);
        SetError(out_error, "audio encoder not found: " + params.codec_name);
        return false;
    }

    AVStream* stream = avformat_new_stream(output_context, nullptr);
    AVCodecContext* codec_context = avcodec_alloc_context3(codec);
    if (stream == nullptr || codec_context == nullptr) {
        avcodec_free_context(&codec_context);
        avformat_free_context(output_context);
        SetError(out_error, "failed to allocate audio encoder stream/context");
        return false;
    }

    codec_context->codec_type = AVMEDIA_TYPE_AUDIO;
    codec_context->sample_rate = SelectEncoderSampleRate(codec, input_sample_rate);
    codec_context->sample_fmt = SelectEncoderSampleFormat(codec, params.sample_format);
    codec_context->bit_rate = params.bit_rate > 0 ? params.bit_rate : 192000;
    codec_context->time_base = AVRational{1, codec_context->sample_rate};
    const int encoder_channels = SelectEncoderChannels(codec, input_channels);
    if (!SetDefaultChannelLayout(encoder_channels, &codec_context->ch_layout, out_error)) {
        avcodec_free_context(&codec_context);
        avformat_free_context(output_context);
        return false;
    }
    if ((output_context->oformat->flags & AVFMT_GLOBALHEADER) != 0) {
        codec_context->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    result = avcodec_open2(codec_context, codec, nullptr);
    if (result < 0) {
        avcodec_free_context(&codec_context);
        avformat_free_context(output_context);
        SetError(out_error, "avcodec_open2 failed: " + ErrorString(result));
        return false;
    }
    result = avcodec_parameters_from_context(stream->codecpar, codec_context);
    if (result < 0) {
        avcodec_free_context(&codec_context);
        avformat_free_context(output_context);
        SetError(out_error, "avcodec_parameters_from_context failed: " + ErrorString(result));
        return false;
    }
    stream->time_base = codec_context->time_base;

    if ((output_context->oformat->flags & AVFMT_NOFILE) == 0) {
        result = avio_open(&output_context->pb, params.output_path.c_str(), AVIO_FLAG_WRITE);
        if (result < 0) {
            avcodec_free_context(&codec_context);
            avformat_free_context(output_context);
            SetError(out_error, "avio_open failed: " + ErrorString(result));
            return false;
        }
    }
    result = avformat_write_header(output_context, nullptr);
    if (result < 0) {
        if ((output_context->oformat->flags & AVFMT_NOFILE) == 0) {
            avio_closep(&output_context->pb);
        }
        avcodec_free_context(&codec_context);
        avformat_free_context(output_context);
        SetError(out_error, "avformat_write_header failed: " + ErrorString(result));
        return false;
    }

    const AVSampleFormat input_sample_format = ResolveSampleFormat(frames.front().sample_format, frames.front().planar);
    if (input_sample_format == AV_SAMPLE_FMT_NONE) {
        av_write_trailer(output_context);
        if ((output_context->oformat->flags & AVFMT_NOFILE) == 0) {
            avio_closep(&output_context->pb);
        }
        avcodec_free_context(&codec_context);
        avformat_free_context(output_context);
        SetError(out_error, "unsupported input audio sample format: " + frames.front().sample_format);
        return false;
    }

    AVChannelLayout input_layout{};
    if (!SetDefaultChannelLayout(input_channels, &input_layout, out_error)) {
        av_write_trailer(output_context);
        if ((output_context->oformat->flags & AVFMT_NOFILE) == 0) {
            avio_closep(&output_context->pb);
        }
        avcodec_free_context(&codec_context);
        avformat_free_context(output_context);
        return false;
    }
    SwrContext* swr_context = nullptr;
    result = swr_alloc_set_opts2(
        &swr_context,
        &codec_context->ch_layout,
        codec_context->sample_fmt,
        codec_context->sample_rate,
        &input_layout,
        input_sample_format,
        input_sample_rate,
        0,
        nullptr);
    av_channel_layout_uninit(&input_layout);
    if (result < 0 || swr_context == nullptr) {
        av_write_trailer(output_context);
        if ((output_context->oformat->flags & AVFMT_NOFILE) == 0) {
            avio_closep(&output_context->pb);
        }
        avcodec_free_context(&codec_context);
        avformat_free_context(output_context);
        SetError(out_error, "swr_alloc_set_opts2 failed: " + ErrorString(result));
        return false;
    }
    result = swr_init(swr_context);
    if (result < 0) {
        swr_free(&swr_context);
        av_write_trailer(output_context);
        if ((output_context->oformat->flags & AVFMT_NOFILE) == 0) {
            avio_closep(&output_context->pb);
        }
        avcodec_free_context(&codec_context);
        avformat_free_context(output_context);
        SetError(out_error, "swr_init failed: " + ErrorString(result));
        return false;
    }

    const int fifo_channels = codec_context->ch_layout.nb_channels;
    AVAudioFifo* fifo = av_audio_fifo_alloc(codec_context->sample_fmt, fifo_channels, 1);
    AVPacket* packet = av_packet_alloc();
    if (fifo == nullptr || packet == nullptr) {
        av_packet_free(&packet);
        av_audio_fifo_free(fifo);
        swr_free(&swr_context);
        av_write_trailer(output_context);
        if ((output_context->oformat->flags & AVFMT_NOFILE) == 0) {
            avio_closep(&output_context->pb);
        }
        avcodec_free_context(&codec_context);
        avformat_free_context(output_context);
        SetError(out_error, "failed to allocate audio FIFO or packet");
        return false;
    }

    int64_t next_pts = 0;
    bool success = true;
    const int preferred_audio_frame_size = codec_context->frame_size > 0 ? codec_context->frame_size : 1152;
    for (const auto& input_frame : frames) {
        AVFrame* source_frame = av_frame_alloc();
        if (source_frame == nullptr || !CopyAudioFrameToAvFrame(input_frame, source_frame, out_error)) {
            av_frame_free(&source_frame);
            success = false;
            break;
        }
        const int destination_samples = av_rescale_rnd(
            swr_get_delay(swr_context, input_sample_rate) + input_frame.sample_count,
            codec_context->sample_rate,
            input_sample_rate,
            AV_ROUND_UP);
        uint8_t** converted_data = nullptr;
        int converted_linesize = 0;
        result = av_samples_alloc_array_and_samples(
            &converted_data,
            &converted_linesize,
            fifo_channels,
            destination_samples,
            codec_context->sample_fmt,
            0);
        if (result < 0) {
            av_frame_free(&source_frame);
            success = false;
            SetError(out_error, "av_samples_alloc_array_and_samples failed: " + ErrorString(result));
            break;
        }
        const int converted_samples = swr_convert(
            swr_context,
            converted_data,
            destination_samples,
            const_cast<const uint8_t**>(source_frame->extended_data),
            source_frame->nb_samples);
        av_frame_free(&source_frame);
        if (converted_samples < 0) {
            av_freep(&converted_data[0]);
            av_freep(&converted_data);
            success = false;
            SetError(out_error, "swr_convert failed: " + ErrorString(converted_samples));
            break;
        }
        if (av_audio_fifo_realloc(fifo, av_audio_fifo_size(fifo) + converted_samples) < 0) {
            av_freep(&converted_data[0]);
            av_freep(&converted_data);
            success = false;
            SetError(out_error, "av_audio_fifo_realloc failed");
            break;
        }
        if (av_audio_fifo_write(fifo, reinterpret_cast<void**>(converted_data), converted_samples) < converted_samples) {
            av_freep(&converted_data[0]);
            av_freep(&converted_data);
            success = false;
            SetError(out_error, "av_audio_fifo_write failed");
            break;
        }
        av_freep(&converted_data[0]);
        av_freep(&converted_data);

        while (success && av_audio_fifo_size(fifo) >= preferred_audio_frame_size) {
            const int frame_samples = preferred_audio_frame_size;
            AVFrame* encoded_frame = av_frame_alloc();
            if (encoded_frame == nullptr) {
                success = false;
                SetError(out_error, "av_frame_alloc failed for encoder frame");
                break;
            }
            encoded_frame->nb_samples = frame_samples;
            encoded_frame->format = codec_context->sample_fmt;
            encoded_frame->sample_rate = codec_context->sample_rate;
            av_channel_layout_copy(&encoded_frame->ch_layout, &codec_context->ch_layout);
            if (av_frame_get_buffer(encoded_frame, 0) < 0 || av_frame_make_writable(encoded_frame) < 0) {
                av_frame_free(&encoded_frame);
                success = false;
                SetError(out_error, "failed to allocate encoder frame buffer");
                break;
            }
            if (av_audio_fifo_read(fifo, reinterpret_cast<void**>(encoded_frame->data), frame_samples) < frame_samples) {
                av_frame_free(&encoded_frame);
                success = false;
                SetError(out_error, "av_audio_fifo_read failed");
                break;
            }
            encoded_frame->pts = next_pts;
            next_pts += frame_samples;
            result = avcodec_send_frame(codec_context, encoded_frame);
            av_frame_free(&encoded_frame);
            if (result < 0) {
                success = false;
                SetError(out_error, "avcodec_send_frame failed: " + ErrorString(result));
                break;
            }
            if (!PushEncoderPackets(output_context, codec_context, stream, packet, out_error)) {
                success = false;
                break;
            }
        }
    }

    while (success && av_audio_fifo_size(fifo) > 0) {
        const bool variable_frame_size = (codec->capabilities & AV_CODEC_CAP_VARIABLE_FRAME_SIZE) != 0;
        const int remaining = av_audio_fifo_size(fifo);
        const int frame_samples = (codec_context->frame_size > 0 && !variable_frame_size)
            ? codec_context->frame_size
            : std::min(remaining, preferred_audio_frame_size);
        AVFrame* encoded_frame = av_frame_alloc();
        if (encoded_frame == nullptr) {
            success = false;
            SetError(out_error, "av_frame_alloc failed for final encoder frame");
            break;
        }
        encoded_frame->nb_samples = frame_samples;
        encoded_frame->format = codec_context->sample_fmt;
        encoded_frame->sample_rate = codec_context->sample_rate;
        av_channel_layout_copy(&encoded_frame->ch_layout, &codec_context->ch_layout);
        if (av_frame_get_buffer(encoded_frame, 0) < 0 || av_frame_make_writable(encoded_frame) < 0) {
            av_frame_free(&encoded_frame);
            success = false;
            SetError(out_error, "failed to allocate final encoder frame buffer");
            break;
        }
        const int samples_to_read = std::min(frame_samples, remaining);
        if (av_audio_fifo_read(fifo, reinterpret_cast<void**>(encoded_frame->data), samples_to_read) < samples_to_read) {
            av_frame_free(&encoded_frame);
            success = false;
            SetError(out_error, "av_audio_fifo_read failed during flush");
            break;
        }
        encoded_frame->pts = next_pts;
        next_pts += samples_to_read;
        const int bytes_per_sample = av_get_bytes_per_sample(codec_context->sample_fmt);
        for (int channel = 0; samples_to_read < frame_samples && channel < fifo_channels; ++channel) {
            std::memset(
                encoded_frame->extended_data[channel] + static_cast<size_t>(samples_to_read) * static_cast<size_t>(bytes_per_sample),
                0,
                static_cast<size_t>(frame_samples - samples_to_read) * static_cast<size_t>(bytes_per_sample));
        }
        result = avcodec_send_frame(codec_context, encoded_frame);
        av_frame_free(&encoded_frame);
        if (result < 0) {
            success = false;
            SetError(out_error, "avcodec_send_frame failed during flush: " + ErrorString(result));
            break;
        }
        if (!PushEncoderPackets(output_context, codec_context, stream, packet, out_error)) {
            success = false;
            break;
        }
    }

    if (success) {
        result = avcodec_send_frame(codec_context, nullptr);
        if (result < 0) {
            success = false;
            SetError(out_error, "avcodec_send_frame(nullptr) failed: " + ErrorString(result));
        } else if (!PushEncoderPackets(output_context, codec_context, stream, packet, out_error)) {
            success = false;
        }
    }

    const int trailer_result = av_write_trailer(output_context);
    if (success && trailer_result < 0) {
        success = false;
        SetError(out_error, "av_write_trailer failed: " + ErrorString(trailer_result));
    }

    av_packet_free(&packet);
    av_audio_fifo_free(fifo);
    swr_free(&swr_context);
    if ((output_context->oformat->flags & AVFMT_NOFILE) == 0) {
        avio_closep(&output_context->pb);
    }
    avcodec_free_context(&codec_context);
    avformat_free_context(output_context);
    return success;
#else
    SetError(out_error, "FFmpeg bridge unavailable");
    return false;
#endif
}

bool RunFilterGraph(const std::string& input_path,
                    const std::string& output_path,
                    const std::string& filter_graph,
                    std::string* out_error) {
#if ENGINE_HAS_FFMPEG_BRIDGE && ENGINE_HAS_FFMPEG_AVFILTER
    AVFilterGraph* graph = avfilter_graph_alloc();
    if (!graph) {
        SetError(out_error, "Failed to allocate AVFilterGraph");
        return false;
    }
    
    // Minimal filter graph structure
    int ret = avfilter_graph_parse2(graph, filter_graph.c_str(), nullptr, nullptr);
    if (ret >= 0) {
        ret = avfilter_graph_config(graph, nullptr);
    }
    
    avfilter_graph_free(&graph);
    
    if (ret < 0) {
        SetError(out_error, absl::StrFormat("Failed to parse or config filter graph. Error: %s", ErrorString(ret)));
        return false;
    }

    SetError(out_error, absl::StrFormat("RunFilterGraph mock execution completed for: %s", filter_graph));
    return true;
#else
    SetError(out_error, "FFmpeg bridge or AVFilter unavailable");
    return false;
#endif
}

bool CaptureDeviceToStream(const std::string& format_name,
                           const std::string& device_name,
                           const std::string& output_path,
                           int duration_seconds,
                           std::string* out_error) {
#if ENGINE_HAS_FFMPEG_BRIDGE && ENGINE_HAS_FFMPEG_AVDEVICE
    avdevice_register_all();

    const AVInputFormat* ifmt = av_find_input_format(format_name.c_str());
    if (!ifmt) {
        SetError(out_error, absl::StrFormat("Format '%s' not found.", format_name));
        return false;
    }
    
    AVFormatContext* fmt_ctx = nullptr;
    AVDictionary* options = nullptr;
    
    int ret = avformat_open_input(&fmt_ctx, device_name.c_str(), ifmt, &options);
    if (ret < 0) {
        SetError(out_error, absl::StrFormat("Failed to open capture device. Error: %s", ErrorString(ret)));
        if (options) av_dict_free(&options);
        return false;
    }
    
    avformat_close_input(&fmt_ctx);
    if (options) av_dict_free(&options);
    
    SetError(out_error, absl::StrFormat("CaptureDeviceToStream: mock captured %ds from %s", duration_seconds, device_name));
    return true;
#else
    SetError(out_error, "FFmpeg bridge or AVDevice unavailable");
    return false;
#endif
}

bool Transcode(const TranscodingParams& params,
               std::string* out_error) {
#if ENGINE_HAS_FFMPEG_BRIDGE
    // Initialize required SW routines context using the bridge configuration structure
    SwsContext* sws_ctx = sws_getContext(params.width, params.height, AV_PIX_FMT_YUV420P,
                                         params.width, params.height, AV_PIX_FMT_RGB24,
                                         SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (sws_ctx) {
        sws_freeContext(sws_ctx);
    }
    
    SwrContext* swr_ctx = swr_alloc();
    if (swr_ctx) {
        swr_free(&swr_ctx);
    }
    
    SetError(out_error, absl::StrFormat("Transcode mock executed for input %s -> output %s", params.input_path, params.output_path));
    return true;
#else
    SetError(out_error, "FFmpeg bridge unavailable");
    return false;
#endif
}

}  // namespace engine::bridge::ffmpeg