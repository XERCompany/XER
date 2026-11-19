/**
 * XER::Media::MediaPlayerImpl — owns the playback pipeline (design doc):
 * a Demuxer (FFmpeg for src=, ChunkDemuxer for MSE), one DecoderStream
 * per elementary stream, AudioClockTimeSource as the master clock,
 * VideoFrameCompositor ordering video output, PipelineStatistics +
 * WatchTimeRecorder for metrics, and PipelineController enforcing the
 * state machine.
 */
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "media/base/media_types.h"
#include "media/decoders/decoder.h"
#include "media/decoders/decoder_stream.h"
#include "media/demuxer/demuxer.h"
#include "media/metrics/pipeline_statistics.h"
#include "media/metrics/watch_time_recorder.h"
#include "media/player/pipeline_controller.h"
#include "media/renderers/audio_clock_time_source.h"
#include "media/renderers/renderer.h"
#include "media/renderers/time_source.h"
#include "media/renderers/video_frame_compositor.h"

namespace XER::Media {

class MediaPlayerImpl {
public:
    MediaPlayerImpl();
    ~MediaPlayerImpl() = default;
    MediaPlayerImpl(const MediaPlayerImpl&) = delete;
    MediaPlayerImpl& operator=(const MediaPlayerImpl&) = delete;

    // Wires a demuxer + sinks + factory into a playable pipeline.
    bool Initialize(std::unique_ptr<Demuxer> demuxer,
                    RendererFactory* factory,
                    AudioRendererSink* audio_sink,
                    VideoRendererSink* video_sink,
                    std::string* error_out);

    bool Open(const std::string& path, std::string* error_out);
    void Close();

    void Play();
    void Pause();
    void Stop();
    bool Seek(TimeTicks position, std::string* error_out);
    void SetVolume(double volume);

    // Master clock: audio-derived when an audio stream exists, else
    // wall clock.
    TimeTicks media_time() const;
    TimeTicks duration() const;
    bool playing() const {
        return controller_.Is(PipelineState::kPlaying);
    }

    // Metrics.
    const PipelineStatistics& statistics() const { return stats_; }
    const WatchTimeRecorder& watch_time() const { return watch_time_; }
    PipelineState pipeline_state() const { return controller_.state(); }
    TimeSource* time_source() const { return time_source_; }

    // Latest composited video frame (nullptr before first decode).
    const DecodedFrame* CurrentVideoFrame();

private:
    void OnAudioDecoded(DecodedFrame frame);
    void OnVideoDecoded(DecodedFrame frame);
    void PumpWatchTime();

    std::unique_ptr<Demuxer> demuxer_;
    std::vector<std::unique_ptr<AudioDecoder>> audio_decoders_;
    std::vector<std::unique_ptr<VideoDecoder>> video_decoders_;
    std::unique_ptr<DecoderStream> audio_stream_;
    std::unique_ptr<DecoderStream> video_stream_;
    AudioRendererSink* audio_sink_ = nullptr;
    VideoRendererSink* video_sink_ = nullptr;

    PipelineController controller_;
    std::unique_ptr<AudioClockTimeSource> audio_clock_ts_;
    std::unique_ptr<WallClockTimeSource> wall_ts_;
    TimeSource* time_source_ = nullptr;
    VideoFrameCompositor compositor_;
    PipelineStatistics stats_;
    WatchTimeRecorder watch_time_;
    TimeTicks last_watch_mark_ = kNoTimestamp;
    bool has_audio_ = false;
};

}  // namespace XER::Media
