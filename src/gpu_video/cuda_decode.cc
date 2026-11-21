// cuda_decode.cc — see cuda_decode.h for the contract. The demux /
// pacing / audio-tap logic mirrors src/wrapper/ffmpeg/
// ffmpeg_video_decoder.cc (proven semantics — audio tap queue bounds,
// demux error limit, seek flush behavior are identical); the delta is
// the CUDA hwaccel negotiation in Open() and the hw-frame download in
// NextFrame(). All CUDA contact is runtime: a missing driver or GPU
// downgrades the session to software decode, never an error of its own.
#include "gpu_video/cuda_decode.h"

#include "gpu_video/cuda_runtime.h"

#ifdef _WIN32
#ifndef __STDC_CONSTANT_MACROS
#define __STDC_CONSTANT_MACROS
#endif
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/samplefmt.h>
#include <libswscale/swscale.h>
}
#if XER_GPV_HAS_LIBYUV
#include <libyuv.h>
#endif
#endif

#include <algorithm>
#include <atomic>
#include <climits>
#include <cstdio>
#include <deque>
#include <mutex>

namespace XER::GpuVideo {

namespace {

#ifdef _WIN32

// The authoritative CUDA check: creating a CUDA hw device context runs
// cuInit + device enumeration inside FFmpeg's own delay-loaded driver
// bindings. Failure here is the ground truth for "no usable CUDA" —
// cheaper DLL probes in the report are informational only.
bool TryCreateCudaDevice(AVBufferRef** out) {
    if (out == nullptr) return false;
    *out = nullptr;
    return av_hwdevice_ctx_create(out, AV_HWDEVICE_TYPE_CUDA, nullptr,
                                  nullptr, 0) == 0 &&
           *out != nullptr;
}

const CudaDecodeProbe& ComputeProbe() {
    static const CudaDecodeProbe probe = [] {
        CudaDecodeProbe p;
        p.ffmpeg_hwaccel_cuda =
            av_hwdevice_find_type_by_name("cuda") != AV_HWDEVICE_TYPE_NONE;
        p.nvcuda_dll = detail::CudaDriverLoadable();
        p.nvcuvid_dll = detail::NvcuvidLoadable();
        p.decoder_h264_cuvid =
            avcodec_find_decoder_by_name("h264_cuvid") != nullptr;
        p.decoder_hevc_cuvid =
            avcodec_find_decoder_by_name("hevc_cuvid") != nullptr;
        p.decoder_av1_cuvid =
            avcodec_find_decoder_by_name("av1_cuvid") != nullptr;
        AVBufferRef* dev = nullptr;
        p.cuda_device = TryCreateCudaDevice(&dev);
        av_buffer_unref(&dev);
        if (!p.ffmpeg_hwaccel_cuda) {
            p.detail = "ffmpeg build lacks the cuda hwaccel";
        } else if (!p.nvcuda_dll) {
            p.detail = "CUDA driver library not loadable (no NVIDIA "
                       "driver?)";
        } else if (!p.cuda_device) {
            p.detail = "CUDA device init failed (driver present, no "
                       "usable GPU?)";
        } else {
            p.detail = "cuda hwaccel ready";
        }
        return p;
    }();
    return probe;
}

#else  // !_WIN32

const CudaDecodeProbe& ComputeProbe() {
    static const CudaDecodeProbe probe = [] {
        CudaDecodeProbe p;
        p.nvcuda_dll = detail::CudaDriverLoadable();
        p.nvcuvid_dll = detail::NvcuvidLoadable();
        p.detail = "video decode unsupported on this platform";
        return p;
    }();
    return probe;
}

#endif

// Mirrors CollectAudioFrame() in ffmpeg_engine_bridge.cc: copies one
// decoded AVFrame into AudioFrameInfo with raw plane bytes.
#ifdef _WIN32
bool CollectTapAudioFrame(const AVFrame* frame,
                          int stream_index,
                          std::vector<XER::Bridge::FFmpeg::AudioFrameInfo>*
                              out_frames) {
    const AVSampleFormat sample_format =
        static_cast<AVSampleFormat>(frame->format);
    const int bytes_per_sample = av_get_bytes_per_sample(sample_format);
    if (bytes_per_sample <= 0) {
        return false;
    }
    const bool planar = av_sample_fmt_is_planar(sample_format) != 0;
#if LIBAVUTIL_VERSION_MAJOR >= 57
    const int channels = frame->ch_layout.nb_channels;
    const std::int64_t layout_mask =
        frame->ch_layout.order == AV_CHANNEL_ORDER_NATIVE
            ? static_cast<std::int64_t>(frame->ch_layout.u.mask)
            : 0;
#else
    const int channels = frame->channels;
    const std::int64_t layout_mask =
        static_cast<std::int64_t>(frame->channel_layout);
#endif
    const int plane_count = planar ? std::max(channels, 0) : 1;
    const int plane_size =
        frame->nb_samples * bytes_per_sample *
        (planar ? 1 : std::max(channels, 0));

    XER::Bridge::FFmpeg::AudioFrameInfo info;
    info.stream_index = stream_index;
    info.sample_rate = frame->sample_rate;
    info.channels = channels;
    info.channel_layout = layout_mask;
    info.sample_count = frame->nb_samples;
    info.planar = planar;
    const char* fmt_name = av_get_sample_fmt_name(sample_format);
    info.sample_format = fmt_name != nullptr ? fmt_name : "";
    info.pts = frame->pts;
    info.best_effort_timestamp = frame->best_effort_timestamp;
    info.plane_sizes.reserve(static_cast<size_t>(plane_count));
    info.data.reserve(static_cast<size_t>(
        std::max(plane_count, 0) * std::max(plane_size, 0)));

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
#endif

}  // namespace

const CudaDecodeProbe& ProbeCudaDecode() {
    return ComputeProbe();
}

std::string CudaDecodeProbe::Report() const {
    std::string s;
    const auto line = [&s](const char* k, bool v) {
        s += "  ";
        s += k;
        s += v ? ": yes\n" : ": no\n";
    };
    line("ffmpeg hwaccel 'cuda'", ffmpeg_hwaccel_cuda);
    line("nvcuda driver dll", nvcuda_dll);
    line("nvcuvid dll", nvcuvid_dll);
    line("cuda device ctx", cuda_device);
    line("decoder h264_cuvid", decoder_h264_cuvid);
    line("decoder hevc_cuvid", decoder_hevc_cuvid);
    line("decoder av1_cuvid", decoder_av1_cuvid);
    s += "  cuda_decode available: ";
    s += Available() ? "yes" : "no";
    s += "  (";
    s += detail;
    s += ")\n";
    return s;
}

struct CudaVideoDecoder::Impl {
#ifdef _WIN32
    AVFormatContext* fmt = nullptr;
    AVCodecContext* codec = nullptr;
    const AVCodec* decoder = nullptr;
    SwsContext* sws = nullptr;
    AVStream* stream = nullptr;
    AVPacket* packet = nullptr;
    AVFrame* frame = nullptr;
    AVFrame* sw_frame = nullptr;   // hwaccel download target
    AVFrame* scaled = nullptr;
    int video_stream_index = -1;
    double fps = 30.0;
    double duration = 0.0;
    double position_seconds = -1.0;
    bool has_audio = false;
    int width = 0;
    int height = 0;

    // ---- CUDA hwaccel state ----
    // hw_device is created in Open() before codec open; the decoder
    // negotiates AV_PIX_FMT_CUDA through get_format when it supports
    // the cuda hwaccel. hwaccel_active is set by the get_format
    // callback — the only honest signal the GPU path is live (a codec
    // without cuda support silently stays software).
    AVBufferRef* hw_device = nullptr;
    bool hwaccel_active = false;
    FrameSurface last_surface = FrameSurface::kNone;
    // Lazily built sws context — keyed on the SOFTWARE source format
    // (after hwdownload or plain decode), not codec->pix_fmt, which is
    // AV_PIX_FMT_CUDA while hwaccel is live.
    int sws_src_format = -1;
    int sws_src_w = 0;
    int sws_src_h = 0;

    // ---- Audio tap state (same contract as ffmpeg_video_decoder) ----
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

    int demux_read_errors = 0;
    static constexpr int kDemuxErrorLimit = 3;
    static constexpr size_t kMaxQueuedAudioPackets = 2048;
    static constexpr size_t kQueuedAudioHighWater = 1024;
    std::atomic<std::uint64_t> dropped_audio_packets{0};

    void NoteAudioPacketDrop() {
        if (dropped_audio_packets.fetch_add(
                1, std::memory_order_relaxed) == 0) {
            std::fprintf(stderr,
                         "cuda_decode: audio tap losing packets "
                         "(queue cap %zu) — decoded audio will gap\n",
                         kMaxQueuedAudioPackets);
        }
    }

    size_t QueuedAudioPackets() {
        std::lock_guard<std::mutex> lock(audio_mutex);
        return audio_queue.size();
    }

    void PushAudioPacket(const AVPacket* src) {
        std::lock_guard<std::mutex> lock(audio_mutex);
        if (audio_queue.size() >= kMaxQueuedAudioPackets) {
            av_packet_free(&audio_queue.front());
            audio_queue.pop_front();
            NoteAudioPacketDrop();
        }
        AVPacket* clone = av_packet_alloc();
        if (clone != nullptr && av_packet_ref(clone, src) == 0) {
            audio_queue.push_back(clone);
        } else {
            av_packet_free(&clone);
        }
    }

    void PrepareAudioStream() {
        if (!has_audio || audio_stream != nullptr) {
            return;
        }
        audio_stream = fmt->streams[audio_stream_index];
        audio_start_seconds =
            (audio_stream->start_time != AV_NOPTS_VALUE &&
             audio_stream->time_base.den > 0)
                ? std::max(0.0, audio_stream->start_time *
                                      av_q2d(audio_stream->time_base))
                : 0.0;
        if (audio_tap) {
            audio_codec_desc =
                avcodec_find_decoder(audio_stream->codecpar->codec_id);
            if (audio_codec_desc != nullptr) {
                audio_codec =
                    avcodec_alloc_context3(audio_codec_desc);
                if (audio_codec != nullptr &&
                    avcodec_parameters_to_context(audio_codec,
                                                  audio_stream->codecpar) >= 0 &&
                    avcodec_open2(audio_codec, audio_codec_desc,
                                  nullptr) == 0) {
                    // ready — stays software decode; audio has no
                    // CUDA path
                } else {
                    avcodec_free_context(&audio_codec);
                }
            }
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
        demux_read_errors = 0;
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
        demux_read_errors = 0;
    }

    // get_format negotiation: the decoder calls this during open/first
    // frames with the list of pix_fmts it can output. Picking
    // AV_PIX_FMT_CUDA keeps the decoded pictures on the GPU (downloaded
    // per frame in NextFrame); returning the first SW entry keeps the
    // plain path — chosen for codecs with no cuda hwaccel.
    static AVPixelFormat GetFormatCb(AVCodecContext* ctx,
                                     const AVPixelFormat* fmt) {
        auto* self = static_cast<Impl*>(ctx->opaque);
        const bool want_cuda =
            self != nullptr && self->hw_device != nullptr;
        for (const AVPixelFormat* f = fmt; *f != AV_PIX_FMT_NONE; ++f) {
            if (*f == AV_PIX_FMT_CUDA && want_cuda) {
                self->hwaccel_active = true;
                return AV_PIX_FMT_CUDA;
            }
        }
        return fmt[0];
    }

    // Creates the video codec context. When `use_hw` and the CUDA hw
    // device exists, the context is armed for hwaccel negotiation.
    bool OpenVideoCodec(bool use_hw) {
        decoder = avcodec_find_decoder(stream->codecpar->codec_id);
        if (decoder == nullptr) {
            return false;
        }
        codec = avcodec_alloc_context3(decoder);
        if (codec == nullptr ||
            avcodec_parameters_to_context(codec, stream->codecpar) < 0) {
            return false;
        }
        if (use_hw && hw_device != nullptr) {
            codec->opaque = this;
            codec->get_format = &Impl::GetFormatCb;
            codec->hw_device_ctx = av_buffer_ref(hw_device);
        }
        return avcodec_open2(codec, decoder, nullptr) == 0;
    }

    void ReleaseCodec() {
        if (codec != nullptr) {
            avcodec_free_context(&codec);
        }
        if (sws != nullptr) {
            sws_freeContext(sws);
            sws = nullptr;
        }
        sws_src_format = -1;
        sws_src_w = 0;
        sws_src_h = 0;
        if (frame != nullptr) {
            av_frame_free(&frame);
        }
        if (sw_frame != nullptr) {
            av_frame_free(&sw_frame);
        }
        if (scaled != nullptr) {
            av_frame_free(&scaled);
        }
        if (packet != nullptr) {
            av_packet_free(&packet);
        }
        if (hw_device != nullptr) {
            av_buffer_unref(&hw_device);
        }
        hwaccel_active = false;
        last_surface = FrameSurface::kNone;
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
        width = 0;
        height = 0;
        fps = 30.0;
        position_seconds = -1.0;
    }

    // Converts one SOFTWARE-format AVFrame into packed BGRA. libyuv
    // fast paths for the common YUV layouts (when the module was built
    // with it), swscale otherwise; the sws context is lazily keyed on
    // the actual source format/dims since the incoming format is not
    // knowable at Open() time (hwaccel hides it behind AV_PIX_FMT_CUDA).
    bool ConvertToBgra(const AVFrame* src, uint8_t* dst) {
        const int src_fmt = src->format;
        const int src_w = src->width > 0 ? src->width : width;
        const int src_h = src->height > 0 ? src->height : height;
#if XER_GPV_HAS_LIBYUV
        if (src_fmt == AV_PIX_FMT_YUV420P && src->data[1] &&
            src->data[2]) {
            libyuv::I420ToARGB(
                src->data[0], src->linesize[0],
                src->data[1], src->linesize[1],
                src->data[2], src->linesize[2],
                dst, src_w * 4, src_w, src_h);
            return true;
        }
        if (src_fmt == AV_PIX_FMT_NV12 && src->data[1]) {
            libyuv::NV12ToARGB(
                src->data[0], src->linesize[0],
                src->data[1], src->linesize[1],
                dst, src_w * 4, src_w, src_h);
            return true;
        }
        if (src_fmt == AV_PIX_FMT_YUV422P && src->data[1] &&
            src->data[2]) {
            libyuv::I422ToARGB(
                src->data[0], src->linesize[0],
                src->data[1], src->linesize[1],
                src->data[2], src->linesize[2],
                dst, src_w * 4, src_w, src_h);
            return true;
        }
#endif
        if (sws == nullptr || sws_src_format != src_fmt ||
            sws_src_w != src_w || sws_src_h != src_h) {
            if (sws != nullptr) {
                sws_freeContext(sws);
                sws = nullptr;
            }
            sws = sws_getContext(src_w, src_h,
                                 static_cast<AVPixelFormat>(src_fmt),
                                 width, height, AV_PIX_FMT_BGRA,
                                 SWS_BILINEAR, nullptr, nullptr,
                                 nullptr);
            if (sws == nullptr) {
                return false;
            }
            sws_src_format = src_fmt;
            sws_src_w = src_w;
            sws_src_h = src_h;
        }
        uint8_t* dst_planes[1] = {dst};
        int dst_stride[1] = {width * 4};
        sws_scale(sws, src->data, src->linesize, 0, src_h, dst_planes,
                  dst_stride);
        return true;
    }
#else
    bool audio_tap = false;
    struct Dummy {};
    Dummy dummy;
#endif
};

CudaVideoDecoder::CudaVideoDecoder()
    : impl_(std::make_unique<Impl>()) {}

CudaVideoDecoder::~CudaVideoDecoder() {
    Close();
}

void CudaVideoDecoder::EnableAudioTap(bool enable) {
    impl_->audio_tap = enable;
}

bool CudaVideoDecoder::Open(const std::string& utf8_path,
                            std::string* error_out) {
    Close();
#ifdef _WIN32
    Impl& p = *impl_;
    p.packet = av_packet_alloc();
    p.frame = av_frame_alloc();
    p.sw_frame = av_frame_alloc();
    p.scaled = av_frame_alloc();
    p.audio_frame = av_frame_alloc();
    if (p.packet == nullptr || p.frame == nullptr ||
        p.sw_frame == nullptr || p.scaled == nullptr ||
        p.audio_frame == nullptr) {
        if (error_out) *error_out = "alloc failed";
        Close();
        return false;
    }
    if (avformat_open_input(&p.fmt, utf8_path.c_str(), nullptr,
                            nullptr) != 0) {
        if (error_out) *error_out = "cannot open: " + utf8_path;
        Close();
        return false;
    }
    if (avformat_find_stream_info(p.fmt, nullptr) < 0) {
        if (error_out) *error_out = "no stream info";
        Close();
        return false;
    }
    p.video_stream_index =
        av_find_best_stream(p.fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr,
                            0);
    // attached_pic "video" streams (cover art) are filtered out — same
    // guard as the bridge decoder.
    if (p.video_stream_index >= 0 &&
        (p.fmt->streams[p.video_stream_index]->disposition &
         AV_DISPOSITION_ATTACHED_PIC) != 0) {
        int real_video = -1;
        for (unsigned i = 0; i < p.fmt->nb_streams; ++i) {
            const AVStream* s = p.fmt->streams[i];
            if (s->codecpar->codec_type == AVMEDIA_TYPE_VIDEO &&
                (s->disposition & AV_DISPOSITION_ATTACHED_PIC) == 0) {
                real_video = static_cast<int>(i);
                break;
            }
        }
        p.video_stream_index = real_video;
    }
    p.audio_stream_index =
        av_find_best_stream(p.fmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr,
                            0);
    p.has_audio = p.audio_stream_index >= 0;
    p.duration = (p.fmt->duration != AV_NOPTS_VALUE)
        ? p.fmt->duration / static_cast<double>(AV_TIME_BASE) : 0.0;

    if (p.video_stream_index < 0) {
        // Audio-only container: succeed so the audio tap works.
        if (!p.has_audio) {
            if (error_out) *error_out = "no decodable audio/video streams";
            Close();
            return false;
        }
        p.PrepareAudioStream();
        return true;
    }

    p.stream = p.fmt->streams[p.video_stream_index];

    // CUDA hw device — created once per open. Failure is silent
    // by design: without a driver/GPU the session just decodes in
    // software (the probe reports the reason for diagnostics).
    static_cast<void>(TryCreateCudaDevice(&p.hw_device));

    bool opened = p.OpenVideoCodec(/*use_hw=*/true);
    if (!opened && p.hw_device != nullptr) {
        // hw-armed open failed — retry plain. (avcodec_open2 rarely
        // fails for hwaccel reasons this early, but the retry keeps
        // the fallback honest.)
        avcodec_free_context(&p.codec);
        p.codec = nullptr;
        opened = p.OpenVideoCodec(/*use_hw=*/false);
    }
    if (!opened) {
        // codec may be non-null on a partial failure — release it so
        // IsOpen() stays false and nothing leaks until Close/destruction.
        if (p.codec != nullptr) {
            avcodec_free_context(&p.codec);
        }
        if (error_out) *error_out =
            p.decoder == nullptr ? "unsupported codec" : "codec open failed";
        Close();
        return false;
    }

    p.width = p.codec->width;
    p.height = p.codec->height;
    const AVRational rate = p.stream->avg_frame_rate.den != 0
        ? p.stream->avg_frame_rate : p.stream->r_frame_rate;
    p.fps = (rate.num > 0 && rate.den > 0)
        ? av_q2d(rate) : 30.0;
    p.PrepareAudioStream();
    return true;
#else
    if (error_out) *error_out = "video unsupported on this platform";
    return false;
#endif
}

void CudaVideoDecoder::Close() {
#ifdef _WIN32
    impl_->ReleaseAll();
#endif
}

bool CudaVideoDecoder::IsOpen() const {
#ifdef _WIN32
    return impl_->fmt != nullptr &&
           (impl_->codec != nullptr || impl_->audio_stream != nullptr);
#else
    return false;
#endif
}

bool CudaVideoDecoder::NextFrame(video::Frame* out) {
#ifdef _WIN32
    Impl& p = *impl_;
    if (!IsOpen() || out == nullptr) {
        return false;
    }
    p.last_surface = FrameSurface::kNone;

    if (p.codec == nullptr) {
        // Audio-only container: pump a bounded batch of demux packets
        // into the audio tap queue, then report end-of-stream — same
        // contract as the bridge decoder.
        constexpr int kPumpBudget = 256;
        for (int i = 0; i < kPumpBudget; ++i) {
            if (p.audio_tap && p.audio_codec != nullptr &&
                p.QueuedAudioPackets() >= Impl::kQueuedAudioHighWater) {
                break;
            }
            const int read_ret = av_read_frame(p.fmt, p.packet);
            if (read_ret == AVERROR_EOF) {
                av_packet_unref(p.packet);
                p.audio_demux_eof = true;
                break;
            }
            if (read_ret < 0) {
                if (++p.demux_read_errors >= Impl::kDemuxErrorLimit) {
                    p.audio_demux_eof = true;
                }
                break;
            }
            p.demux_read_errors = 0;
            if (p.audio_tap &&
                p.packet->stream_index == p.audio_stream_index &&
                p.audio_codec != nullptr) {
                p.PushAudioPacket(p.packet);
            }
            av_packet_unref(p.packet);
        }
        return false;
    }

    // Canonical send/receive pacing — drain receive first, feed input
    // only on EAGAIN, flush with a NULL packet at demux EOF (identical
    // to the bridge decoder's fix for the reorder-buffer starvation
    // bug).
    bool flushed = false;
    for (int guard = 0; guard < 8192; ++guard) {
        const int recv_ret = avcodec_receive_frame(p.codec, p.frame);
        if (recv_ret == 0) {
            const int64_t pts = p.frame->best_effort_timestamp !=
                                        AV_NOPTS_VALUE
                                    ? p.frame->best_effort_timestamp
                                    : p.frame->pts;
            if (pts != AV_NOPTS_VALUE && p.stream->time_base.den > 0) {
                p.position_seconds =
                    pts * av_q2d(p.stream->time_base);
            } else {
                p.position_seconds =
                    (p.position_seconds < 0.0 ? 0.0
                                            : p.position_seconds) +
                    1.0 / (p.fps > 0.0 ? p.fps : 30.0);
            }

            // GPU picture? Download it into sw_frame — FFmpeg picks
            // the CPU transfer format (NV12 / P010 for CUDA).
            const AVFrame* src = p.frame;
            bool gpu_frame = false;
            if (p.frame->format == AV_PIX_FMT_CUDA ||
                p.frame->hw_frames_ctx != nullptr) {
                av_frame_unref(p.sw_frame);
                if (av_hwframe_transfer_data(p.sw_frame, p.frame, 0) !=
                        0) {
                    // Download failed — drop the frame and keep the
                    // stream alive rather than failing the session.
                    av_frame_unref(p.frame);
                    continue;
                }
                p.sw_frame->best_effort_timestamp =
                    p.frame->best_effort_timestamp;
                p.sw_frame->pts = p.frame->pts;
                src = p.sw_frame;
                gpu_frame = true;
            }

            const int out_w = src->width > 0 ? src->width : p.width;
            const int out_h = src->height > 0 ? src->height : p.height;
            if (!out->Allocate(out_w, out_h, video::PixelFormat::BGRA8)) {
                av_frame_unref(p.frame);
                return false;
            }
            if (!p.ConvertToBgra(src, out->Data())) {
                av_frame_unref(p.frame);
                return false;
            }
            out->SetPts(static_cast<int64_t>(
                p.position_seconds * 1000000.0));
            p.last_surface =
                gpu_frame ? FrameSurface::kCuda : FrameSurface::kCpu;
            av_frame_unref(p.frame);
            return true;
        }
        if (recv_ret == AVERROR_EOF) {
            return false;
        }
        if (recv_ret != AVERROR(EAGAIN)) {
            return false;
        }
        if (flushed) {
            return false;
        }
        const int read_ret = av_read_frame(p.fmt, p.packet);
        if (read_ret == AVERROR_EOF) {
            av_packet_unref(p.packet);
            p.audio_demux_eof = true;
            avcodec_send_packet(p.codec, nullptr);
            flushed = true;
            continue;
        }
        if (read_ret < 0) {
            if (++p.demux_read_errors >= Impl::kDemuxErrorLimit) {
                p.audio_demux_eof = true;
            }
            return false;
        }
        p.demux_read_errors = 0;
        if (p.packet->stream_index != p.video_stream_index) {
            if (p.audio_tap &&
                p.packet->stream_index == p.audio_stream_index &&
                p.audio_codec != nullptr) {
                if (p.QueuedAudioPackets() >=
                    Impl::kQueuedAudioHighWater) {
                    p.NoteAudioPacketDrop();
                } else {
                    p.PushAudioPacket(p.packet);
                }
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
    (void)out;
    return false;
#endif
}

bool CudaVideoDecoder::Seek(double seconds) {
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
    int ret = avformat_seek_file(p.fmt, -1, INT64_MIN, ts, ts,
                                 AVSEEK_FLAG_BACKWARD);
    if (ret < 0) {
        const int fallback_stream =
            p.video_stream_index >= 0 ? p.video_stream_index
                                      : p.audio_stream_index;
        if (fallback_stream >= 0) {
            const AVStream* fs = p.fmt->streams[fallback_stream];
            const int64_t stream_ts = av_rescale_q(
                ts, AV_TIME_BASE_Q, fs->time_base);
            ret = av_seek_frame(p.fmt, fallback_stream, stream_ts,
                                AVSEEK_FLAG_BACKWARD);
        }
    }
    if (ret < 0) {
        return false;
    }
    if (p.codec != nullptr) {
        avcodec_flush_buffers(p.codec);
    }
    p.FlushAudioState();
    p.position_seconds = target;
    return true;
#else
    (void)seconds;
    return false;
#endif
}

bool CudaVideoDecoder::ReadAudioFrames(
    std::vector<XER::Bridge::FFmpeg::AudioFrameInfo>* out,
    int max_frames) {
#ifdef _WIN32
    Impl& p = *impl_;
    if (!IsOpen() || out == nullptr) {
        return false;
    }
    if (p.audio_codec == nullptr || p.audio_frame == nullptr) {
        // Same drained-terminator guard as the bridge decoder — without
        // it a tap-enabled consumer spins forever on audio-only files.
        if (p.audio_tap && p.audio_demux_eof) {
            std::lock_guard<std::mutex> lock(p.audio_mutex);
            if (p.audio_queue.empty()) {
                p.audio_drained = true;
            }
        }
        return false;
    }
    const int limit = max_frames > 0 ? max_frames : INT_MAX;
    int produced = 0;
    std::lock_guard<std::mutex> lock(p.audio_mutex);
    while (produced < limit) {
        const int recv_ret =
            avcodec_receive_frame(p.audio_codec, p.audio_frame);
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
            avcodec_send_packet(p.audio_codec, nullptr);
            p.audio_flushed = true;
            continue;
        }
        break;
    }
    return true;
#else
    (void)out; (void)max_frames;
    return false;
#endif
}

double CudaVideoDecoder::GetAudioTimeBase() const {
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

double CudaVideoDecoder::GetAudioStartSeconds() const {
#ifdef _WIN32
    return impl_->audio_start_seconds;
#else
    return 0.0;
#endif
}

bool CudaVideoDecoder::AudioDrained() const {
#ifdef _WIN32
    return impl_->audio_drained.load(std::memory_order_acquire);
#else
    return true;
#endif
}

std::uint64_t CudaVideoDecoder::GetAudioEpoch() const {
#ifdef _WIN32
    return impl_->audio_epoch_.load(std::memory_order_acquire);
#else
    return 0;
#endif
}

double CudaVideoDecoder::GetPositionSeconds() const {
#ifdef _WIN32
    return impl_->position_seconds;
#else
    return -1.0;
#endif
}

bool CudaVideoDecoder::HasAudio() const {
#ifdef _WIN32
    return impl_->has_audio;
#else
    return false;
#endif
}

double CudaVideoDecoder::GetFps() const {
#ifdef _WIN32
    return impl_->fps > 0.0 ? impl_->fps : 30.0;
#else
    return 30.0;
#endif
}

double CudaVideoDecoder::GetDurationSeconds() const {
#ifdef _WIN32
    return impl_->duration;
#else
    return 0.0;
#endif
}

int CudaVideoDecoder::GetWidth() const {
#ifdef _WIN32
    return impl_->width;
#else
    return 0;
#endif
}

int CudaVideoDecoder::GetHeight() const {
#ifdef _WIN32
    return impl_->height;
#else
    return 0;
#endif
}

bool CudaVideoDecoder::HwaccelActive() const {
#ifdef _WIN32
    return impl_->hwaccel_active;
#else
    return false;
#endif
}

FrameSurface CudaVideoDecoder::LastFrameSurface() const {
#ifdef _WIN32
    return impl_->last_surface;
#else
    return FrameSurface::kNone;
#endif
}

std::string CudaVideoDecoder::ActiveHwaccelName() const {
#ifdef _WIN32
    return impl_->hwaccel_active ? "cuda" : "";
#else
    return {};
#endif
}

}  // namespace XER::GpuVideo
