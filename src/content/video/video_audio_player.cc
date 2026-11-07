#include "video_audio_player.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <thread>
#include <utility>

#ifdef _WIN32
#ifndef __STDC_CONSTANT_MACROS
#define __STDC_CONSTANT_MACROS
#endif
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#endif

#include "wrapper/ffmpeg/ffmpeg_engine_bridge.h"
#include "wrapper/ffmpeg/ffmpeg_video_decoder.h"

namespace XER::Content::Video {

namespace {

#ifdef _WIN32
constexpr double kDriftSeekSeconds = 0.08;
// Tolerance for treating a queued chunk as "at" the playhead rather than
// a gap to skip over.
constexpr double kGapToleranceSec = 0.03;
#endif

// Linear-interpolation resampler for the (rare) case where the stream rate
// differs from the device mix rate. Video preview does not need a windowed-
// sinc resampler; aliasing at these rates is inaudible in practice.
std::vector<float> ResampleLinear(const std::vector<float>& in, int in_rate,
                                  int out_rate) {
    if (in.empty() || in_rate <= 0 || out_rate <= 0 || in_rate == out_rate) {
        return in;
    }
    const double ratio = static_cast<double>(out_rate) / in_rate;
    const std::size_t out_frames =
        static_cast<std::size_t>(in.size() * ratio) + 2;
    std::vector<float> out(out_frames, 0.0f);
    for (std::size_t i = 0; i < out_frames; ++i) {
        const double pos = i / ratio;
        const std::size_t i0 = static_cast<std::size_t>(pos);
        const std::size_t i1 = (std::min)(i0 + 1, in.size() - 1);
        const float frac = static_cast<float>(pos - i0);
        out[i] = in[i0] * (1.0f - frac) + in[i1] * frac;
    }
    return out;
}

constexpr std::int64_t kNoPts =
    (std::numeric_limits<std::int64_t>::min)();  // AV_NOPTS_VALUE

} // namespace

VideoAudioPlayer::VideoAudioPlayer() = default;

VideoAudioPlayer::~VideoAudioPlayer() { Close(); }

bool VideoAudioPlayer::Attach(
    std::shared_ptr<XER::engine::bridge::ffmpeg::VideoDecoder> decoder,
    std::string* error_out) {
    Close();
    if (!decoder || !decoder->IsOpen()) {
        if (error_out != nullptr) *error_out = "decoder not open";
        return false;
    }
    av_decoder_ = std::move(decoder);
    audio_time_base_ = av_decoder_->GetAudioTimeBase();
    const bool has_audio = av_decoder_->HasAudio();
    has_audio_.store(has_audio, std::memory_order_release);
    if (!has_audio) {
        if (ready_cb_) ready_cb_(false, "no audio stream");
        return true;  // nothing to play — engine reports HasAudio()=false
    }
    failed_.store(false, std::memory_order_relaxed);
    ready_.store(false, std::memory_order_relaxed);
    ready_notified_ = false;
    format_known_.store(false, std::memory_order_relaxed);
    output_ready_.store(false, std::memory_order_relaxed);
    stream_stop_.store(false, std::memory_order_release);
    worker_done_.store(false, std::memory_order_relaxed);
    playhead_sec_.store(0.0, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(audio_mutex_);
        pending_src_.clear();
        play_queue_.clear();
    }
    {
        std::lock_guard<std::mutex> lock(decode_mutex_);
        load_error_.clear();
    }
    worker_ = std::thread([this] { StreamWorker(); });
    return true;
}

void VideoAudioPlayer::Close() {
    stream_stop_.store(true, std::memory_order_release);
    if (worker_.joinable()) worker_.join();
    Pause();
    {
        std::lock_guard<std::mutex> lock(audio_mutex_);
        pending_src_.clear();
        play_queue_.clear();
    }
    {
        std::lock_guard<std::mutex> lock(decode_mutex_);
        worker_done_ = false;
        load_error_.clear();
        src_rate_ = 0;
        src_channels_ = 0;
    }
    format_known_.store(false, std::memory_order_relaxed);
    output_ready_.store(false, std::memory_order_relaxed);
    av_decoder_.reset();
    audio_time_base_ = 0.0;
    playhead_sec_.store(0.0, std::memory_order_relaxed);
    out_rate_ = 0;
    out_channels_ = 0;
    CleanupWasapi();
    ready_.store(false, std::memory_order_relaxed);
    failed_.store(false, std::memory_order_relaxed);
    has_audio_.store(false, std::memory_order_relaxed);
    ready_notified_ = false;
}

bool VideoAudioPlayer::PollReady() {
    if (ready_.load(std::memory_order_acquire)) return true;
    if (failed_.load(std::memory_order_acquire)) return false;
    if (!format_known_.load(std::memory_order_acquire)) return false;
    if (!output_ready_.exchange(true, std::memory_order_acq_rel)) {
        if (!FinalizeOutput()) {
            failed_.store(true, std::memory_order_release);
            return false;
        }
    }
    // First successful finalize → notify once; the engine turns this into
    // its kAudioReady command and starts playback honoring play intent.
    if (!ready_notified_) {
        ready_notified_ = true;
        if (ready_cb_) ready_cb_(true, "");
    }
    return ready_.load(std::memory_order_acquire);
}

bool VideoAudioPlayer::HasAudio() const {
    return has_audio_.load(std::memory_order_acquire);
}

// ── Decode / convert worker ────────────────────────────────────────────────

void VideoAudioPlayer::StreamWorker() {
#ifdef _WIN32
    auto decoder = av_decoder_;  // keep the decoder alive for this thread
    if (!decoder) {
        worker_done_.store(true, std::memory_order_release);
        return;
    }

    SourceChunk cur;
    bool cur_open = false;
    double prev_frame_end_sec = -1.0;  // PTS-less fallback clock
    double prev_frame_dur = 0.0;
    bool saw_eof = false;

    auto pts_sec = [&](std::int64_t pts, std::int64_t bet) -> double {
        const std::int64_t v =
            (bet != kNoPts && bet != (std::numeric_limits<std::int64_t>::max)())
                ? bet
                : pts;
        if (v == kNoPts || v == (std::numeric_limits<std::int64_t>::min)() ||
            audio_time_base_ <= 0.0) {
            return -1.0;
        }
        return static_cast<double>(v) * audio_time_base_;
    };

    std::vector<XER::engine::bridge::ffmpeg::AudioFrameInfo> frames;
    while (!stream_stop_.load(std::memory_order_acquire)) {
        if (!decoder->IsOpen()) break;
        frames.clear();
        if (!decoder->ReadAudioFrames(&frames, 64)) {
            {
                std::lock_guard<std::mutex> lock(decode_mutex_);
                load_error_ = "audio decode failed";
            }
            break;
        }

        // Flush pending source chunks once enough audio is banked (or the
        // stream ended) and the device format is known.
        const auto maybe_convert = [&]() {
            if (!output_ready_.load(std::memory_order_acquire)) return;
            std::deque<SourceChunk> batch;
            {
                std::lock_guard<std::mutex> lock(audio_mutex_);
                std::size_t total = 0;
                for (const auto& c : pending_src_) total += c.frames;
                const bool drain = saw_eof || stream_stop_.load();
                if (!drain && total < kConvertThreshold) return;
                batch.swap(pending_src_);
            }
            for (auto& chunk : batch) {
                // Drop chunks decoded before the latest seek/flush — their
                // timestamps belong to the old position.
                if (chunk.epoch != decoder->GetAudioEpoch()) continue;
                const double dur =
                    chunk.frames > 0 && src_rate_ > 0
                        ? static_cast<double>(chunk.frames) / src_rate_
                        : 0.0;
                if (dur > 0.0 &&
                    chunk.start_sec + dur <
                        playhead_sec_.load(std::memory_order_relaxed) - 0.5) {
                    continue;  // wholly in the past — seek already passed it
                }
                ConvertChunk(chunk);
            }
            audio_cv_.notify_all();
        };

        if (frames.empty()) {
            if (decoder->AudioDrained()) {
                saw_eof = true;
                maybe_convert();
                break;
            }
            maybe_convert();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }

        for (const auto& frame : frames) {
            // Learn the stream format from the first decoded frame.
            if (!format_known_.load(std::memory_order_acquire)) {
                if (frame.sample_rate <= 0 || frame.channels <= 0 ||
                    frame.channels > 8) {
                    std::lock_guard<std::mutex> lock(decode_mutex_);
                    load_error_ = "unsupported audio layout";
                    goto done;
                }
                {
                    std::lock_guard<std::mutex> lock(decode_mutex_);
                    src_rate_ = frame.sample_rate;
                    src_channels_ = frame.channels;
                }
                format_known_.store(true, std::memory_order_release);
            }
            if (frame.sample_rate != src_rate_ ||
                frame.channels != src_channels_) {
                continue;  // mid-stream format change — skip the odd frame
            }

            const double pts = pts_sec(frame.best_effort_timestamp, frame.pts);
            const double frame_dur =
                src_rate_ > 0
                    ? static_cast<double>(frame.sample_count) / src_rate_
                    : 0.0;
            double sec;
            if (pts >= 0.0) {
                sec = pts;
            } else if (prev_frame_end_sec >= 0.0) {
                sec = prev_frame_end_sec;
            } else {
                sec = 0.0;
            }
            prev_frame_end_sec = sec + frame_dur;
            prev_frame_dur = frame_dur;

            if (!cur_open) {
                cur.channels.assign(
                    static_cast<std::size_t>(src_channels_),
                    std::vector<float>());
                cur.start_sec = sec;
                cur.epoch = decoder->GetAudioEpoch();
                cur_open = true;
            }
            if (!AppendFrameSamples(frame, &cur)) {
                std::lock_guard<std::mutex> lock(decode_mutex_);
                load_error_ =
                    "unsupported sample format: " + frame.sample_format;
                goto done;
            }
            cur.frames += static_cast<std::size_t>(frame.sample_count);

            if (cur.frames >= 8192) {  // ~170 ms at 48 kHz
                {
                    std::lock_guard<std::mutex> lock(audio_mutex_);
                    if (cur.epoch == decoder->GetAudioEpoch()) {
                        pending_src_.push_back(std::move(cur));
                    }
                }
                cur = SourceChunk{};
                cur.channels.assign(
                    static_cast<std::size_t>(src_channels_),
                    std::vector<float>());
                cur_open = false;
            }
        }
        maybe_convert();
    }

done:
    // Flush the tail of the stream.
    if (cur_open && cur.frames > 0) {
        std::lock_guard<std::mutex> lock(audio_mutex_);
        if (cur.epoch == decoder->GetAudioEpoch()) {
            pending_src_.push_back(std::move(cur));
        }
    }
    {
        // Final conversion pass — drain everything left, ignoring the
        // threshold.
        std::deque<SourceChunk> batch;
        {
            std::lock_guard<std::mutex> lock(audio_mutex_);
            batch.swap(pending_src_);
        }
        for (auto& chunk : batch) {
            if (chunk.epoch != decoder->GetAudioEpoch()) continue;
            ConvertChunk(chunk);
        }
        audio_cv_.notify_all();
    }
    worker_done_.store(true, std::memory_order_release);
    {
        std::string msg;
        {
            std::lock_guard<std::mutex> lock(decode_mutex_);
            msg = load_error_;
        }
        if (ready_cb_) ready_cb_(msg.empty(), msg);
    }
#else
    (void)0;
#endif
}

// Converts one source-rate chunk into a device-rate PlayChunk and queues it.
// Runs on the worker thread; reads output format fields set by
// FinalizeOutput (written once before output_ready_ is published).
void VideoAudioPlayer::ConvertChunk(const SourceChunk& chunk) {
#ifdef _WIN32
    if (out_rate_ <= 0 || out_channels_ <= 0 || chunk.frames == 0) return;
    const int src_ch = src_channels_;
    const int dst_ch = out_channels_;
    if (src_ch <= 0 || src_ch > 8 ||
        chunk.channels.size() != static_cast<std::size_t>(src_ch)) {
        return;
    }

    // Channel map (same policy the old whole-file path used):
    // mono→N duplicate, N→mono average, otherwise first min(src,dst).
    std::vector<std::vector<float>> mapped(
        static_cast<std::size_t>(dst_ch));
    for (int c = 0; c < dst_ch; ++c) {
        if (src_ch == 1) {
            mapped[static_cast<std::size_t>(c)] = chunk.channels.front();
        } else if (dst_ch == 1) {
            auto& m = mapped.front();
            m.assign(chunk.frames, 0.0f);
            for (const auto& s : chunk.channels) {
                for (std::size_t i = 0; i < chunk.frames; ++i) {
                    m[i] += s[i] / static_cast<float>(src_ch);
                }
            }
        } else {
            const int sc = (std::min)(c, src_ch - 1);
            mapped[static_cast<std::size_t>(c)] =
                chunk.channels[static_cast<std::size_t>(sc)];
        }
    }
    for (auto& ch : mapped) {
        if (src_rate_ != out_rate_) {
            ch = ResampleLinear(ch, src_rate_, out_rate_);
        }
    }

    PlayChunk pc;
    pc.channels = dst_ch;
    pc.start_sec = chunk.start_sec;
    const std::size_t frames =
        mapped.empty() ? 0 : mapped.front().size();
    pc.frames = frames;
    pc.data.assign(frames * static_cast<std::size_t>(dst_ch), 0.0f);
    for (std::size_t i = 0; i < frames; ++i) {
        for (int c = 0; c < dst_ch; ++c) {
            pc.data[i * static_cast<std::size_t>(dst_ch) +
                   static_cast<std::size_t>(c)] = mapped[c][i];
        }
    }
    pc.end_sec = chunk.start_sec +
                 static_cast<double>(frames) / out_rate_;

    std::lock_guard<std::mutex> lock(audio_mutex_);
    // Bound the queue: ~32 MB of float PCM ≈ 80+ s stereo — far beyond any
    // legitimate lead the demuxer can build.
    std::size_t queued = 0;
    for (const auto& q : play_queue_) queued += q.data.size();
    if (queued + pc.data.size() >
        static_cast<std::size_t>(8u * 1024u * 1024u)) {
        play_queue_.clear();  // consumer stalled — restart cleanly
    }
    play_queue_.push_back(std::move(pc));
#endif
}

bool VideoAudioPlayer::FinalizeOutput() {
#ifdef _WIN32
    if (ready_.load(std::memory_order_acquire)) return true;
    if (!InitWasapi()) return false;

    // WASAPI shared mode hands us the device mix format — float32
    // interleaved on every modern Windows audio endpoint.
    WAVEFORMATEX* fmt = nullptr;
    if (audio_client_ == nullptr ||
        FAILED(audio_client_->GetMixFormat(&fmt)) || fmt == nullptr) {
        return false;
    }
    out_rate_ = static_cast<int>(fmt->nSamplesPerSec);
    out_channels_ = static_cast<int>(fmt->nChannels);
    const bool is_float =
        fmt->wFormatTag == WAVE_FORMAT_IEEE_FLOAT ||
        (fmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
         reinterpret_cast<WAVEFORMATEXTENSIBLE*>(fmt)->SubFormat ==
             KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
    CoTaskMemFree(fmt);
    if (!is_float || out_rate_ <= 0 || out_channels_ <= 0 ||
        out_channels_ > 8) {
        CleanupWasapi();
        return false;
    }
    if (FAILED(audio_client_->GetBufferSize(&buffer_frame_count_))) {
        CleanupWasapi();
        return false;
    }
    ready_.store(true, std::memory_order_release);
    return true;
#else
    return false;
#endif
}

#ifdef _WIN32
bool VideoAudioPlayer::InitWasapi() {
    if (audio_client_ != nullptr) return true;

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) return false;

    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                          CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                          reinterpret_cast<void**>(&enumerator_));
    if (FAILED(hr)) goto fail;

    hr = enumerator_->GetDefaultAudioEndpoint(eRender, eConsole, &device_);
    if (FAILED(hr)) goto fail;

    hr = device_->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                           reinterpret_cast<void**>(&audio_client_));
    if (FAILED(hr)) goto fail;

    {
        WAVEFORMATEX* mix = nullptr;
        hr = audio_client_->GetMixFormat(&mix);
        if (FAILED(hr) || mix == nullptr) goto fail;
        const REFERENCE_TIME buffer_duration = 2000000;  // 200 ms
        hr = audio_client_->Initialize(AUDCLNT_SHAREMODE_SHARED, 0,
                                       buffer_duration, 0, mix, nullptr);
        CoTaskMemFree(mix);
        if (FAILED(hr)) goto fail;
    }

    hr = audio_client_->GetService(__uuidof(IAudioRenderClient),
                                   reinterpret_cast<void**>(&render_client_));
    if (FAILED(hr)) goto fail;
    return true;

fail:
    CleanupWasapi();
    return false;
}

void VideoAudioPlayer::CleanupWasapi() {
    if (render_client_) { render_client_->Release(); render_client_ = nullptr; }
    if (audio_client_) { audio_client_->Release(); audio_client_ = nullptr; }
    if (device_) { device_->Release(); device_ = nullptr; }
    if (enumerator_) { enumerator_->Release(); enumerator_ = nullptr; }
    buffer_frame_count_ = 0;
    CoUninitialize();
}
#endif

// ── Sample-format conversion (unchanged from the whole-file path) ──────────

bool VideoAudioPlayer::AppendFrameSamples(
    const XER::engine::bridge::ffmpeg::AudioFrameInfo& frame, SourceChunk* chunk) {
    const int channels = frame.channels;
    const int samples = frame.sample_count;
    if (channels <= 0 || samples <= 0 || chunk == nullptr) return false;
    if (chunk->channels.size() != static_cast<std::size_t>(channels))
        return false;

    const std::string fmt = frame.sample_format;
    const bool planar = frame.planar;
    const std::vector<int>& sizes = frame.plane_sizes;
    const std::vector<std::uint8_t>& data = frame.data;

    auto need = [&](std::size_t bytes) {
        return data.size() >= bytes;
    };

    // Byte offset of plane `p` inside the concatenated `data` blob.
    auto plane_offset = [&](int p) -> std::size_t {
        std::size_t off = 0;
        for (int i = 0; i < p && i < static_cast<int>(sizes.size()); ++i) {
            off += static_cast<std::size_t>(sizes[i]);
        }
        return off;
    };

    const int ch = channels;
    if (fmt == "fltp") {  // planar f32 — the common AAC decode output
        if (!planar || sizes.size() < static_cast<std::size_t>(ch))
            return false;
        for (int c = 0; c < ch; ++c) {
            const std::size_t off = plane_offset(c);
            const std::size_t bytes =
                static_cast<std::size_t>(samples) * sizeof(float);
            if (!need(off + bytes)) return false;
            const float* src = reinterpret_cast<const float*>(data.data() + off);
            auto& dst = chunk->channels[static_cast<std::size_t>(c)];
            dst.insert(dst.end(), src, src + samples);
        }
        return true;
    }
    if (fmt == "flt") {  // packed f32
        const std::size_t bytes =
            static_cast<std::size_t>(samples) * ch * sizeof(float);
        if (!need(bytes)) return false;
        const float* src = reinterpret_cast<const float*>(data.data());
        for (int c = 0; c < ch; ++c) {
            auto& dst = chunk->channels[static_cast<std::size_t>(c)];
            dst.reserve(dst.size() + samples);
            for (int i = 0; i < samples; ++i) {
                dst.push_back(src[static_cast<std::size_t>(i) * ch + c]);
            }
        }
        return true;
    }
    if (fmt == "s16" || fmt == "s16p") {
        const int bytes_per = 2;
        const int plane_count = planar ? ch : 1;
        if (static_cast<int>(sizes.size()) < plane_count) return false;
        for (int c = 0; c < ch; ++c) {
            const int plane = planar ? c : 0;
            const std::size_t off = plane_offset(plane);
            auto& dst = chunk->channels[static_cast<std::size_t>(c)];
            dst.reserve(dst.size() + samples);
            for (int i = 0; i < samples; ++i) {
                std::size_t idx;
                if (planar) {
                    idx = off + static_cast<std::size_t>(i) * bytes_per;
                } else {
                    idx = off +
                          (static_cast<std::size_t>(i) * ch + c) *
                              bytes_per;
                }
                if (!need(idx + bytes_per)) return false;
                std::int16_t v;
                std::memcpy(&v, data.data() + idx, sizeof(v));
                dst.push_back(static_cast<float>(v) / 32768.0f);
            }
        }
        return true;
    }
    if (fmt == "s32" || fmt == "s32p") {
        const int bytes_per = 4;
        const int plane_count = planar ? ch : 1;
        if (static_cast<int>(sizes.size()) < plane_count) return false;
        for (int c = 0; c < ch; ++c) {
            const int plane = planar ? c : 0;
            const std::size_t off = plane_offset(plane);
            auto& dst = chunk->channels[static_cast<std::size_t>(c)];
            dst.reserve(dst.size() + samples);
            for (int i = 0; i < samples; ++i) {
                std::size_t idx;
                if (planar) {
                    idx = off + static_cast<std::size_t>(i) * bytes_per;
                } else {
                    idx = off +
                          (static_cast<std::size_t>(i) * ch + c) *
                              bytes_per;
                }
                if (!need(idx + bytes_per)) return false;
                std::int32_t v;
                std::memcpy(&v, data.data() + idx, sizeof(v));
                dst.push_back(static_cast<float>(v) / 2147483648.0f);
            }
        }
        return true;
    }
    if (fmt == "u8" || fmt == "u8p") {
        const int plane_count = planar ? ch : 1;
        if (static_cast<int>(sizes.size()) < plane_count) return false;
        for (int c = 0; c < ch; ++c) {
            const int plane = planar ? c : 0;
            const std::size_t off = plane_offset(plane);
            auto& dst = chunk->channels[static_cast<std::size_t>(c)];
            dst.reserve(dst.size() + samples);
            for (int i = 0; i < samples; ++i) {
                std::size_t idx = planar
                                      ? off + static_cast<std::size_t>(i)
                                      : off + static_cast<std::size_t>(i) * ch +
                                            c;
                if (!need(idx + 1)) return false;
                dst.push_back(
                    (static_cast<float>(data[idx]) - 128.0f) / 128.0f);
            }
        }
        return true;
    }
    if (fmt == "dbl" || fmt == "dblp") {
        const int bytes_per = 8;
        const int plane_count = planar ? ch : 1;
        if (static_cast<int>(sizes.size()) < plane_count) return false;
        for (int c = 0; c < ch; ++c) {
            const int plane = planar ? c : 0;
            const std::size_t off = plane_offset(plane);
            auto& dst = chunk->channels[static_cast<std::size_t>(c)];
            dst.reserve(dst.size() + samples);
            for (int i = 0; i < samples; ++i) {
                std::size_t idx;
                if (planar) {
                    idx = off + static_cast<std::size_t>(i) * bytes_per;
                } else {
                    idx = off +
                          (static_cast<std::size_t>(i) * ch + c) *
                              bytes_per;
                }
                if (!need(idx + bytes_per)) return false;
                double v;
                std::memcpy(&v, data.data() + idx, sizeof(v));
                dst.push_back(static_cast<float>(v));
            }
        }
        return true;
    }
    return false;
}

// ── Transport ──────────────────────────────────────────────────────────────

void VideoAudioPlayer::Play() {
#ifdef _WIN32
    if (!ready_.load(std::memory_order_acquire) ||
        audio_client_ == nullptr || playing_.load(std::memory_order_acquire)) {
        return;
    }
    // Reap a finished render thread before starting a new one — assigning
    // to a joinable std::thread would terminate the process.
    if (render_thread_.joinable()) render_thread_.join();
    if (FAILED(audio_client_->Start())) return;
    playing_.store(true, std::memory_order_release);
    render_stop_.store(false, std::memory_order_release);
    render_thread_ = std::thread([this] { RenderThreadMain(); });
#endif
}

void VideoAudioPlayer::Pause() {
#ifdef _WIN32
    if (!playing_.load(std::memory_order_acquire) &&
        !render_thread_.joinable()) {
        return;
    }
    playing_.store(false, std::memory_order_release);
    render_stop_.store(true, std::memory_order_release);
    if (render_thread_.joinable()) render_thread_.join();
    if (audio_client_ != nullptr) audio_client_->Stop();
#endif
}

void VideoAudioPlayer::Stop() {
    Pause();
    playhead_sec_.store(0.0, std::memory_order_relaxed);
}

void VideoAudioPlayer::Seek(double seconds) {
    double limit = (std::numeric_limits<double>::max)();
    if (av_decoder_) {
        const double dur = av_decoder_->GetDurationSeconds();
        if (dur > 0.0) limit = dur;
    }
    playhead_sec_.store(
        std::clamp(seconds, 0.0, limit), std::memory_order_relaxed);
    // Drop queued device audio — it belongs to the old position. Source
    // chunks still in flight are filtered by timestamp/epoch downstream.
    {
        std::lock_guard<std::mutex> lock(audio_mutex_);
        play_queue_.clear();
    }
    audio_cv_.notify_all();
}

void VideoAudioPlayer::SyncTo(double video_position_seconds) {
    const double audio_pos = GetPosition();
    if (audio_pos < 0.0) return;
    if (std::abs(audio_pos - video_position_seconds) > kDriftSeekSeconds) {
        Seek(video_position_seconds);
    }
}

double VideoAudioPlayer::GetPosition() const {
    return has_audio_.load(std::memory_order_acquire)
               ? playhead_sec_.load(std::memory_order_relaxed)
               : -1.0;
}

double VideoAudioPlayer::GetDuration() const {
    return av_decoder_ ? av_decoder_->GetDurationSeconds() : 0.0;
}

// ── WASAPI render thread ───────────────────────────────────────────────────

void VideoAudioPlayer::RenderThreadMain() {
#ifdef _WIN32
    unsigned int cw = 0;
    _controlfp_s(&cw, _DN_FLUSH, _MCW_DN);

    while (!render_stop_.load(std::memory_order_acquire)) {
        if (!playing_.load(std::memory_order_acquire)) break;

        UINT32 padding = 0;
        if (FAILED(audio_client_->GetCurrentPadding(&padding))) break;
        const UINT32 available = buffer_frame_count_ - padding;
        if (available == 0) {
            Sleep(5);
            continue;
        }

        BYTE* data = nullptr;
        if (FAILED(render_client_->GetBuffer(available, &data))) break;
        float* out = reinterpret_cast<float*>(data);
        const int ch = out_channels_;
        const double rate = static_cast<double>(out_rate_);

        double playhead = playhead_sec_.load(std::memory_order_relaxed);
        std::size_t filled = 0;
        bool queue_empty = false;
        {
            std::lock_guard<std::mutex> lock(audio_mutex_);
            while (filled < available) {
                // Drop chunks that ended before the playhead.
                while (!play_queue_.empty() &&
                       play_queue_.front().end_sec <= playhead) {
                    play_queue_.pop_front();
                }
                if (play_queue_.empty()) {
                    queue_empty = true;
                    break;
                }
                PlayChunk& c = play_queue_.front();
                if (c.start_sec > playhead) {
                    // Gap before the next chunk: emit silence only up to
                    // its start so we don't overshoot and drop it.
                    const double gap = c.start_sec - playhead;
                    std::size_t gap_frames = static_cast<std::size_t>(
                        (std::min)(gap * rate,
                                static_cast<double>(available - filled)));
                    if (gap_frames == 0) break;  // sub-sample gap
                    const std::size_t base = filled * ch;
                    std::memset(out + base, 0,
                                gap_frames * ch * sizeof(float));
                    filled += gap_frames;
                    playhead += static_cast<double>(gap_frames) / rate;
                    continue;
                }
                const double off_sec = playhead - c.start_sec;
                std::size_t off_frames = static_cast<std::size_t>(
                    std::clamp(off_sec * rate, 0.0,
                               static_cast<double>(c.frames)));
                const std::size_t remain = c.frames - off_frames;
                const std::size_t n =
                    (std::min)(remain, available - filled);
                const float* src = c.data.data() + off_frames * ch;
                float* dst = out + filled * ch;
                for (std::size_t i = 0; i < n * static_cast<std::size_t>(ch);
                     ++i) {
                    dst[i] = src[i] * volume_;
                }
                filled += n;
                playhead += static_cast<double>(n) / rate;
                if (off_frames + n >= c.frames) {
                    play_queue_.pop_front();
                }
            }
        }
        if (filled < available) {
            // Starved: silence for the remainder but keep the playhead
            // advancing so A/V position stays aligned — late chunks are
            // skipped by the past-check above rather than replayed.
            const std::size_t base = filled * ch;
            std::memset(out + base, 0,
                        (available - filled) * ch * sizeof(float));
            playhead += static_cast<double>(available - filled) / rate;
        }
        playhead_sec_.store(playhead, std::memory_order_relaxed);
        render_client_->ReleaseBuffer(available, 0);

        // End of stream: worker finished and nothing is queued.
        if (queue_empty && worker_done_.load(std::memory_order_acquire)) {
            playing_.store(false, std::memory_order_release);
            audio_client_->Stop();
            break;
        }
        Sleep(1);
    }
#endif
}

} // namespace XER::Content::Video
