#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <mmdeviceapi.h>
#include <audioclient.h>
#endif

namespace XER::engine::bridge::ffmpeg {
struct AudioFrameInfo;  // fwd — full type only needed in the .cc
class VideoDecoder;
}

namespace XER::Content::Video {

// Dedicated audio player for the video preview pipeline.
//
// Unlike XER::Content::Audio::AudioPlayer (the editor's file player), this class
// owns the whole video-audio path inside src/content/video:
//   - Attach() binds to the VideoSession's shared ffmpeg::VideoDecoder —
//     the same demuxer/decoder instance the video path already uses, so
//     the container is opened exactly once;
//   - a worker thread drains decoder->ReadAudioFrames() into source-rate
//     chunks, converts them to the WASAPI device format (channel map +
//     linear resample) and queues device-rate chunks;
//   - output goes to its own WASAPI shared-mode render client;
//   - transport (Play/Pause/Stop/Seek) plus SyncTo() for A/V drift
//     correction against the video clock.
//
// Timing model: everything is in media seconds on the container timeline.
// Each queued chunk carries the media timestamp of its first frame (from
// the audio stream PTS), so seeks line up regardless of how much audio was
// buffered. The playhead only advances while playing; underflow outputs
// silence and keeps advancing so a late chunk is skipped rather than
// replayed.
//
// Threading contract:
//   - Attach/Close/PollReady/Play/Pause/Stop/Seek/SyncTo: caller thread
//     (the UI thread in practice);
//   - decode/convert worker: internal thread;
//   - WASAPI render: internal thread, alive only while playing.
class VideoAudioPlayer {
public:
    VideoAudioPlayer();
    ~VideoAudioPlayer();

    using ReadyCallback = std::function<void(bool ok, const std::string& message)>;
    void SetReadyCallback(ReadyCallback callback) { ready_cb_ = std::move(callback); }

    // Binds to the session's shared decoder (must already be Open()ed with
    // EnableAudioTap). Returns false only for trivial rejections; stream
    // errors are reported through the ready callback / PollReady().
    bool Attach(std::shared_ptr<XER::engine::bridge::ffmpeg::VideoDecoder> decoder,
                std::string* error_out = nullptr);
    void Close();

    // Call on the UI thread (e.g. from VideoEngine::Tick). Initializes the
    // WASAPI device once the stream format is known; returns true when
    // playable.
    bool PollReady();
    bool HasAudio() const;

    void Play();
    void Pause();
    void Stop();
    void Seek(double seconds);
    // Hard-seek when the audio clock drifts from the video position.
    void SyncTo(double video_position_seconds);

    double GetPosition() const;
    double GetDuration() const;
    bool IsPlaying() const { return playing_.load(std::memory_order_acquire); }

private:
    // Source-rate (stream-native) decoded audio, per-channel planar float.
    struct SourceChunk {
        std::vector<std::vector<float>> channels;
        std::size_t frames = 0;
        double start_sec = 0.0;   // media time of first frame (PTS-based)
        std::uint64_t epoch = 0;  // decoder audio epoch at decode time
    };

    // Device-rate interleaved float chunk ready for WASAPI output.
    struct PlayChunk {
        std::vector<float> data;  // frames * channels interleaved
        std::size_t frames = 0;
        int channels = 0;
        double start_sec = 0.0;   // media time of first frame
        double end_sec = 0.0;     // media time one past the last frame
    };

    void StreamWorker();
    void ConvertChunk(const SourceChunk& chunk);
    bool FinalizeOutput();       // WASAPI init + mix-format capture
    bool InitWasapi();
    void CleanupWasapi();
    void RenderThreadMain();

    // Converts one decoded AudioFrameInfo into per-channel float samples
    // appended to `chunk`. Supports the formats avcodec emits for common
    // audio codecs (fltp/flt/s16/s16p/s32/s32p/u8/dbl).
    bool AppendFrameSamples(const XER::engine::bridge::ffmpeg::AudioFrameInfo& frame,
                            SourceChunk* chunk);

    std::shared_ptr<XER::engine::bridge::ffmpeg::VideoDecoder> av_decoder_;
    double audio_time_base_ = 0.0;  // pts * tb = media seconds

    std::thread worker_;
    std::atomic<bool> stream_stop_{false};
    std::atomic<bool> worker_done_{false};
    std::atomic<bool> format_known_{false};
    std::atomic<bool> output_ready_{false};
    std::mutex decode_mutex_;   // guards load_error_ / src format fields
    std::string load_error_;
    int src_rate_ = 0;
    int src_channels_ = 0;

    // Worker -> render queues. `audio_mutex_` guards both deques; the
    // worker notifies `audio_cv_` after pushing play chunks.
    std::mutex audio_mutex_;
    std::condition_variable audio_cv_;
    std::deque<SourceChunk> pending_src_;
    std::deque<PlayChunk> play_queue_;
    // Conversion flush threshold in source frames (~0.7 s at 48 kHz).
    static constexpr std::size_t kConvertThreshold = 32768;

    // Device-side output format (known after FinalizeOutput).
    int out_rate_ = 0;
    int out_channels_ = 0;

#ifdef _WIN32
    IMMDeviceEnumerator* enumerator_ = nullptr;
    IMMDevice* device_ = nullptr;
    IAudioClient* audio_client_ = nullptr;
    IAudioRenderClient* render_client_ = nullptr;
    UINT32 buffer_frame_count_ = 0;
#endif

    std::thread render_thread_;
    std::atomic<bool> render_stop_{false};
    std::atomic<bool> playing_{false};
    // Media-time playhead in seconds; advanced by the render thread.
    std::atomic<double> playhead_sec_{0.0};
    float volume_ = 1.0f;

    std::atomic<bool> ready_{false};
    std::atomic<bool> failed_{false};
    std::atomic<bool> has_audio_{false};
    bool ready_notified_ = false;  // ready_cb_(true) fired once
    ReadyCallback ready_cb_;
};

} // namespace XER::Content::Video
