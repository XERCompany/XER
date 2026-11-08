#include "wrapper/ffmpeg/ffmpeg_video_decoder.h"
#include "wrapper/ffmpeg/ffmpeg_engine_bridge.h"

#ifdef _WIN32
#ifndef __STDC_CONSTANT_MACROS
#define __STDC_CONSTANT_MACROS
#endif
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/samplefmt.h>
#include <libswscale/swscale.h>
#include <libyuv.h>
}
#endif

#include <algorithm>
#include <climits>
#include <mutex>

namespace XER::Bridge::FFmpeg {

namespace {

// Mirrors CollectAudioFrame() in ffmpeg_engine_bridge.cc: copies one decoded
// AVFrame into AudioFrameInfo with raw plane bytes so consumers can convert
// to whatever sample layout they need.
bool CollectTapAudioFrame(const AVFrame* frame,
                          int stream_index,
                          std::vector<AudioFrameInfo>* out_frames) {
    const AVSampleFormat sample_format =
        static_cast<AVSampleFormat>(frame->format);
    const int bytes_per_sample = av_get_bytes_per_sample(sample_format);
    if (bytes_per_sample <= 0) {
        return false;
    }
    const bool planar = av_sample_fmt_is_planar(sample_format) != 0;
#if LIBAVUTIL_VERSION_MAJOR >= 57
    const int channels = frame->ch_layout.nb_channels;
#else
    const int channels = frame->channels;
#endif
    const int plane_count = planar ? std::max(channels, 0) : 1;
    const int plane_size =
        frame->nb_samples * bytes_per_sample * (planar ? 1 : std::max(channels, 0));

    AudioFrameInfo info;
    info.stream_index = stream_index;
    info.sample_rate = frame->sample_rate;
    info.channels = channels;
    info.sample_count = frame->nb_samples;
    info.planar = planar;
    const char* fmt_name = av_get_sample_fmt_name(sample_format);
    info.sample_format = fmt_name != nullptr ? fmt_name : "";
    info.pts = frame->pts;
    info.best_effort_timestamp = frame->best_effort_timestamp;
    info.plane_sizes.reserve(static_cast<size_t>(plane_count));
    info.data.reserve(
        static_cast<size_t>(std::max(plane_count, 0) * std::max(plane_size, 0)));

    for (int plane = 0; plane < plane_count; ++plane) {
        if (frame->extended_data == nullptr ||
            frame->extended_data[plane] == nullptr) {
            return false;
        }
        info.plane_sizes.push_back(plane_size);
        const uint8_t* begin = frame->extended_data[plane];
        info.data.insert(info.data.end(), begin, begin + plane_size);
    }
    out_frames->push_back(std::move(info));
    return true;
}

}  // namespace

struct VideoDecoder::Impl {
#ifdef _WIN32
    AVFormatContext* fmt = nullptr;
    AVCodecContext* codec = nullptr;
    const AVCodec* decoder = nullptr;
    SwsContext* sws = nullptr;
    AVStream* stream = nullptr;
    AVPacket* packet = nullptr;
    AVFrame* frame = nullptr;
    AVFrame* scaled = nullptr;
    int video_stream_index = -1;
    double fps = 30.0;
    double duration = 0.0;
    double position_seconds = -1.0;
    bool has_audio = false;
    int width = 0;
    int height = 0;

    // ---- Audio tap state ----
    // audio_tap is set via EnableAudioTap() before Open(). Everything else is
    // prepared in Open() and then used concurrently: NextFrame() (video
    // thread) pushes cloned packets under audio_mutex; ReadAudioFrames()
    // (audio thread) pops and decodes them under the same mutex.
    bool audio_tap = false;
    int audio_stream_index = -1;
    AVStream* audio_stream = nullptr;
    AVCodecContext* audio_codec = nullptr;
    const AVCodec* audio_codec_desc = nullptr;
    AVFrame* audio_frame = nullptr;
    std::deque<AVPacket*> audio_queue;
    std::mutex audio_mutex;
    std::atomic<bool> audio_demux_eof{false};
    std::atomic<bool> audio_flushed{false};
    std::atomic<bool> audio_drained{false};
    std::atomic<std::uint64_t> audio_epoch_{0};
    double audio_start_seconds = 0.0;

    // ~2048 AAC frames ≈ 40+ s of audio — bounds queue memory if the audio
    // consumer stalls; oldest packets are dropped first.
    static constexpr size_t kMaxQueuedAudioPackets = 2048;

    void PushAudioPacket(const AVPacket* src) {
        std::lock_guard<std::mutex> lock(audio_mutex);
        if (audio_queue.size() >= kMaxQueuedAudioPackets) {
            av_packet_free(&audio_queue.front());
            audio_queue.pop_front();
        }
        AVPacket* clone = av_packet_alloc();
        if (clone != nullptr && av_packet_ref(clone, src) == 0) {
            audio_queue.push_back(clone);
        } else {
            av_packet_free(&clone);
        }
    }

    void ClearAudioQueue() {
        while (!audio_queue.empty()) {
            av_packet_free(&audio_queue.front());
            audio_queue.pop_front();
        }
    }

    void FlushAudioState() {
        audio_epoch_.fetch_add(1, std::memory_order_acq_rel);
        std::lock_guard<std::mutex> lock(audio_mutex);
        ClearAudioQueue();
        if (audio_codec != nullptr) {
            avcodec_flush_buffers(audio_codec);
        }
        audio_flushed = false;
        audio_drained = false;
        audio_demux_eof = false;
    }

    void ReleaseAudio() {
        {
            std::lock_guard<std::mutex> lock(audio_mutex);
            ClearAudioQueue();
        }
        if (audio_codec != nullptr) {
            avcodec_free_context(&audio_codec);
        }
        if (audio_frame != nullptr) {
            av_frame_free(&audio_frame);
        }
        audio_codec = nullptr;
        audio_codec_desc = nullptr;
        audio_stream = nullptr;
        audio_stream_index = -1;
        audio_demux_eof = false;
        audio_flushed = false;
        audio_drained = false;
    }

    void ReleaseCodec() {
        if (codec != nullptr) {
            avcodec_free_context(&codec);
        }
        if (sws != nullptr) {
            sws_freeContext(sws);
            sws = nullptr;
        }
        if (frame != nullptr) {
            av_frame_free(&frame);
        }
        if (scaled != nullptr) {
            av_frame_free(&scaled);
        }
        if (packet != nullptr) {
            av_packet_free(&packet);
        }
    }

    void ReleaseAll() {
        ReleaseCodec();
        ReleaseAudio();
        if (fmt != nullptr) {
            avformat_close_input(&fmt);
        }
        fmt = nullptr;
        stream = nullptr;
        video_stream_index = -1;
        audio_start_seconds = 0.0;
    }
#else
    struct Dummy {};
    Dummy dummy;
#endif
};

VideoDecoder::VideoDecoder() : impl_(std::make_unique<Impl>()) {}

VideoDecoder::~VideoDecoder() {
    Close();
}

void VideoDecoder::EnableAudioTap(bool enable) {
    impl_->audio_tap = enable;
}

bool VideoDecoder::Open(const std::string& path, std::string* error_out) {
    Close();
#ifdef _WIN32
    Impl& p = *impl_;
    p.packet = av_packet_alloc();
    p.frame = av_frame_alloc();
    p.scaled = av_frame_alloc();
    p.audio_frame = av_frame_alloc();
    if (p.packet == nullptr || p.frame == nullptr || p.scaled == nullptr ||
        p.audio_frame == nullptr) {
        if (error_out) *error_out = "alloc failed";
        return false;
    }
    if (avformat_open_input(&p.fmt, path.c_str(), nullptr, nullptr) != 0) {
        if (error_out) *error_out = "cannot open: " + path;
        return false;
    }
    if (avformat_find_stream_info(p.fmt, nullptr) < 0) {
        if (error_out) *error_out = "no stream info";
        return false;
    }
    p.video_stream_index = av_find_best_stream(p.fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (p.video_stream_index < 0) {
        if (error_out) *error_out = "no video stream";
        return false;
    }
    p.stream = p.fmt->streams[p.video_stream_index];
    p.decoder = avcodec_find_decoder(p.stream->codecpar->codec_id);
    if (p.decoder == nullptr) {
        if (error_out) *error_out = "unsupported codec";
        return false;
    }
    p.codec = avcodec_alloc_context3(p.decoder);
    if (p.codec == nullptr ||
        avcodec_parameters_to_context(p.codec, p.stream->codecpar) < 0 ||
        avcodec_open2(p.codec, p.decoder, nullptr) < 0) {
        if (error_out) *error_out = "codec open failed";
        return false;
    }
    p.width = p.codec->width;
    p.height = p.codec->height;
    const AVRational rate = p.stream->avg_frame_rate.den != 0
        ? p.stream->avg_frame_rate : p.stream->r_frame_rate;
    p.fps = (rate.num > 0 && rate.den > 0)
        ? av_q2d(rate) : 30.0;
    p.duration = (p.fmt->duration != AV_NOPTS_VALUE)
        ? p.fmt->duration / AV_TIME_BASE : 0.0;
    p.audio_stream_index =
        av_find_best_stream(p.fmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    p.has_audio = p.audio_stream_index >= 0;
    if (p.has_audio) {
        p.audio_stream = p.fmt->streams[p.audio_stream_index];
        p.audio_start_seconds =
            (p.audio_stream->start_time != AV_NOPTS_VALUE &&
             p.audio_stream->time_base.den > 0)
                ? std::max(0.0, p.audio_stream->start_time *
                                      av_q2d(p.audio_stream->time_base))
                : 0.0;
        if (p.audio_tap) {
            // Decode-side codec for the tapped packets; opened once here so
            // ReadAudioFrames() never touches avformat state.
            p.audio_codec_desc =
                avcodec_find_decoder(p.audio_stream->codecpar->codec_id);
            if (p.audio_codec_desc != nullptr) {
                p.audio_codec =
                    avcodec_alloc_context3(p.audio_codec_desc);
                if (p.audio_codec != nullptr &&
                    avcodec_parameters_to_context(p.audio_codec,
                                                  p.audio_stream->codecpar) >= 0 &&
                    avcodec_open2(p.audio_codec, p.audio_codec_desc,
                                  nullptr) == 0) {
                    // ready
                } else {
                    avcodec_free_context(&p.audio_codec);
                }
            }
        }
    }
    p.sws = sws_getContext(p.width, p.height, p.codec->pix_fmt,
                           p.width, p.height, AV_PIX_FMT_BGRA,
                           SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (p.sws == nullptr) {
        if (error_out) *error_out = "sws init failed";
        return false;
    }
    return true;
#else
    if (error_out) *error_out = "video unsupported on this platform";
    return false;
#endif
}

void VideoDecoder::Close() {
#ifdef _WIN32
    impl_->ReleaseAll();
#endif
}

bool VideoDecoder::IsOpen() const {
#ifdef _WIN32
    return impl_->fmt != nullptr && impl_->codec != nullptr;
#else
    return false;
#endif
}

bool VideoDecoder::NextFrame(std::vector<uint8_t>& bgra, int& width, int& height) {
#ifdef _WIN32
    Impl& p = *impl_;
    if (!IsOpen()) {
        return false;
    }
    bgra.assign(static_cast<size_t>(p.width) * p.height * 4u, 0);
    width = p.width;
    height = p.height;

    // Canonical send/receive pacing: avcodec_send_packet may only be called
    // after avcodec_receive_frame reported EAGAIN (decoder input drained).
    // The previous version sent a packet then received at most one frame —
    // once the reorder buffer still held output, the next send_packet
    // returned EAGAIN and callers read that as end-of-stream, which made
    // playback loop at roughly the decoder's buffer depth (~2 s on some
    // files). Here we always drain receive first, feed input only on
    // EAGAIN, and flush with a NULL packet at demuxer EOF so buffered
    // frames still get delivered before the real end is reported.
    bool flushed = false;  // NULL packet already sent to drain the decoder
    for (int guard = 0; guard < 8192; ++guard) {
        const int recv_ret = avcodec_receive_frame(p.codec, p.frame);
        if (recv_ret == 0) {
            // Track presentation time for position reporting.
            const int64_t pts = p.frame->best_effort_timestamp !=
                                        AV_NOPTS_VALUE
                                    ? p.frame->best_effort_timestamp
                                    : p.frame->pts;
            if (pts != AV_NOPTS_VALUE && p.stream->time_base.den > 0) {
                p.position_seconds =
                    pts * av_q2d(p.stream->time_base);
            } else {
                // No timestamps: estimate from the previous reported
                // position so position still advances monotonically
                // across frames, seeks and loops.
                p.position_seconds =
                    (p.position_seconds < 0.0 ? 0.0 : p.position_seconds) +
                    1.0 / (p.fps > 0.0 ? p.fps : 30.0);
            }

            // Fast path: libyuv SIMD YUV->ARGB for the common pixel formats.
            // FOURCC_ARGB is 0xAARRGGBB -> bytes B,G,R,A = BGRA, matching the
            // canvas D2D1_BITMAP pixel format. Other formats fall back to
            // swscale.
            if (p.codec->pix_fmt == AV_PIX_FMT_YUV420P && p.frame->data[1] &&
                p.frame->data[2]) {
                libyuv::I420ToARGB(
                    p.frame->data[0], p.frame->linesize[0],
                    p.frame->data[1], p.frame->linesize[1],
                    p.frame->data[2], p.frame->linesize[2], bgra.data(),
                    p.width * 4, p.width, p.height);
            } else if (p.codec->pix_fmt == AV_PIX_FMT_NV12 &&
                       p.frame->data[1]) {
                libyuv::NV12ToARGB(
                    p.frame->data[0], p.frame->linesize[0],
                    p.frame->data[1], p.frame->linesize[1], bgra.data(),
                    p.width * 4, p.width, p.height);
            } else if (p.codec->pix_fmt == AV_PIX_FMT_YUV422P &&
                       p.frame->data[1] && p.frame->data[2]) {
                libyuv::I422ToARGB(
                    p.frame->data[0], p.frame->linesize[0],
                    p.frame->data[1], p.frame->linesize[1],
                    p.frame->data[2], p.frame->linesize[2], bgra.data(),
                    p.width * 4, p.width, p.height);
            } else {
                uint8_t* dst[1] = {bgra.data()};
                int dst_stride[1] = {p.width * 4};
                sws_scale(p.sws, p.frame->data, p.frame->linesize, 0,
                          p.height, dst, dst_stride);
            }
            av_frame_unref(p.frame);
            return true;
        }
        if (recv_ret == AVERROR_EOF) {
            // Decoder drained after the flush — genuine end of stream.
            return false;
        }
        if (recv_ret != AVERROR(EAGAIN)) {
            return false;  // real decode error
        }
        // EAGAIN → decoder is ready for more input.
        if (flushed) {
            return false;  // already flushed; nothing more will arrive
        }
        const int read_ret = av_read_frame(p.fmt, p.packet);
        if (read_ret == AVERROR_EOF) {
            av_packet_unref(p.packet);
            // Demuxer exhausted — tell the audio side no more packets will
            // arrive so it can flush and drain its decoder.
            p.audio_demux_eof = true;
            // Demuxer exhausted — flush once so the reorder tail still
            // produces frames; the next receive calls return them, then
            // AVERROR_EOF above reports the true end.
            avcodec_send_packet(p.codec, nullptr);
            flushed = true;
            continue;
        }
        if (read_ret < 0) {
            return false;
        }
        if (p.packet->stream_index != p.video_stream_index) {
            // Audio packets are tapped into the shared queue instead of
            // being dropped — ReadAudioFrames() decodes them off-thread.
            if (p.audio_tap && p.packet->stream_index == p.audio_stream_index &&
                p.audio_codec != nullptr) {
                p.PushAudioPacket(p.packet);
            }
            av_packet_unref(p.packet);
            continue;
        }
        const int send_ret = avcodec_send_packet(p.codec, p.packet);
        av_packet_unref(p.packet);
        if (send_ret < 0) {
            return false;
        }
    }
    return false;
#else
    (void)bgra; (void)width; (void)height;
    return false;
#endif
}

bool VideoDecoder::Seek(double seconds) {
#ifdef _WIN32
    Impl& p = *impl_;
    if (!IsOpen()) {
        return false;
    }
    const double target =
        p.duration > 0.0 ? std::clamp(seconds, 0.0, p.duration)
                         : std::max(0.0, seconds);
    const int64_t ts =
        static_cast<int64_t>(target * static_cast<double>(AV_TIME_BASE));
    // stream_index -1 -> timestamps in AV_TIME_BASE units.
    int ret = avformat_seek_file(p.fmt, -1, INT64_MIN, ts, ts,
                                 AVSEEK_FLAG_BACKWARD);
    if (ret < 0) {
        ret = av_seek_frame(p.fmt, p.video_stream_index, ts,
                            AVSEEK_FLAG_BACKWARD);
    }
    if (ret < 0) {
        return false;
    }
    avcodec_flush_buffers(p.codec);
    // Audio side restarts from the same demux point: drop queued packets
    // from the old position and flush the audio decoder.
    p.FlushAudioState();
    p.position_seconds = target;
    return true;
#else
    (void)seconds;
    return false;
#endif
}

bool VideoDecoder::ReadAudioFrames(std::vector<AudioFrameInfo>* out,
                                   int max_frames) {
#ifdef _WIN32
    Impl& p = *impl_;
    if (!IsOpen() || out == nullptr) {
        return false;
    }
    if (p.audio_codec == nullptr || p.audio_frame == nullptr) {
        return false;  // no audio stream or tap codec failed to open
    }
    const int limit = max_frames > 0 ? max_frames : INT_MAX;
    int produced = 0;
    // The mutex serializes this whole drain against PushAudioPacket()
    // (video thread) and FlushAudioState() (Seek/Close) — each call is
    // bounded to `limit` output frames so contention stays sub-millisecond.
    std::lock_guard<std::mutex> lock(p.audio_mutex);
    while (produced < limit) {
        const int recv_ret = avcodec_receive_frame(p.audio_codec, p.audio_frame);
        if (recv_ret == 0) {
            if (!CollectTapAudioFrame(p.audio_frame, p.audio_stream_index,
                                      out)) {
                av_frame_unref(p.audio_frame);
                return false;
            }
            ++produced;
            av_frame_unref(p.audio_frame);
            continue;
        }
        if (recv_ret == AVERROR_EOF) {
            p.audio_drained = true;
            break;
        }
        if (recv_ret != AVERROR(EAGAIN)) {
            return false;
        }
        // Decoder input drained — feed the next queued packet.
        AVPacket* queued = nullptr;
        if (!p.audio_queue.empty()) {
            queued = p.audio_queue.front();
            p.audio_queue.pop_front();
        }
        if (queued != nullptr) {
            const int send_ret = avcodec_send_packet(p.audio_codec, queued);
            av_packet_free(&queued);
            if (send_ret < 0) {
                return false;
            }
            continue;
        }
        if (p.audio_demux_eof && !p.audio_flushed) {
            // No more packets will ever arrive — flush once so delayed
            // frames still decode, then AVERROR_EOF above reports the end.
            avcodec_send_packet(p.audio_codec, nullptr);
            p.audio_flushed = true;
            continue;
        }
        break;  // queue empty and demux still live — wait for packets
    }
    return true;
#else
    (void)out; (void)max_frames;
    return false;
#endif
}

double VideoDecoder::GetAudioTimeBase() const {
#ifdef _WIN32
    Impl& p = *impl_;
    if (p.audio_stream == nullptr || p.audio_stream->time_base.den <= 0) {
        return 0.0;
    }
    return av_q2d(p.audio_stream->time_base);
#else
    return 0.0;
#endif
}

double VideoDecoder::GetAudioStartSeconds() const {
#ifdef _WIN32
    return impl_->audio_start_seconds;
#else
    return 0.0;
#endif
}

bool VideoDecoder::AudioDrained() const {
#ifdef _WIN32
    return impl_->audio_drained.load(std::memory_order_acquire);
#else
    return true;
#endif
}

std::uint64_t VideoDecoder::GetAudioEpoch() const {
#ifdef _WIN32
    return impl_->audio_epoch_.load(std::memory_order_acquire);
#else
    return 0;
#endif
}

double VideoDecoder::GetPositionSeconds() const {
#ifdef _WIN32
    return impl_->position_seconds;
#else
    return -1.0;
#endif
}

bool VideoDecoder::HasAudio() const {
#ifdef _WIN32
    return impl_->has_audio;
#else
    return false;
#endif
}

double VideoDecoder::GetFps() const {
#ifdef _WIN32
    return impl_->fps > 0.0 ? impl_->fps : 30.0;
#else
    return 30.0;
#endif
}

double VideoDecoder::GetDurationSeconds() const {
#ifdef _WIN32
    return impl_->duration;
#else
    return 0.0;
#endif
}

int VideoDecoder::GetWidth() const {
#ifdef _WIN32
    return impl_->width;
#else
    return 0;
#endif
}

int VideoDecoder::GetHeight() const {
#ifdef _WIN32
    return impl_->height;
#else
    return 0;
#endif
}

}  // namespace XER::Bridge::FFmpeg
