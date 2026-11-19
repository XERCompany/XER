#include "media/player/media_player.h"

#include "media/base/media_log.h"

namespace XER::Media {

MediaPlayerImpl::MediaPlayerImpl() = default;

bool MediaPlayerImpl::Initialize(std::unique_ptr<Demuxer> demuxer,
                                 RendererFactory* factory,
                                 AudioRendererSink* audio_sink,
                                 VideoRendererSink* video_sink,
                                 std::string* error_out) {
    if (!demuxer || !factory) {
        if (error_out) *error_out = "player init: null demuxer or factory";
        return false;
    }
    demuxer_ = std::move(demuxer);
    audio_sink_ = audio_sink;
    video_sink_ = video_sink;
    audio_decoders_ = factory->CreateAudioDecoders();
    video_decoders_ = factory->CreateVideoDecoders();
    wall_ts_ = std::make_unique<WallClockTimeSource>();
    time_source_ = wall_ts_.get();
    return true;
}

bool MediaPlayerImpl::Open(const std::string& path, std::string* error_out) {
    if (!demuxer_) {
        if (error_out) *error_out = "player not initialized";
        return false;
    }
    if (!controller_.BeginOpen(error_out)) return false;
    if (!demuxer_->Open(path, error_out)) {
        controller_.Fail(nullptr);
        return false;
    }

    // Route each elementary stream to the first decoder that accepts it.
    for (auto* s : demuxer_->streams()) {
        if (s->type() == StreamType::kAudio) {
            for (auto& d : audio_decoders_) {
                if (d->CanDecode(s->codec())) {
                    audio_stream_ =
                        std::make_unique<DecoderStream>(s, d.get());
                    has_audio_ = true;
                    break;
                }
            }
        } else if (s->type() == StreamType::kVideo) {
            for (auto& d : video_decoders_) {
                if (d->CanDecode(s->codec())) {
                    video_stream_ =
                        std::make_unique<DecoderStream>(s, d.get());
                    break;
                }
            }
        }
    }
    if (!audio_stream_ && !video_stream_) {
        if (error_out) *error_out = "no decodable streams";
        controller_.Fail(nullptr);
        return false;
    }

    // Audio-master clock when audio exists (per design doc).
    if (has_audio_) {
        int rate = audio_stream_->stream()
                       ? static_cast<int>(audio_stream_->stream()->codec() ==
                                                  CodecId::kUnknown
                                              ? 48000
                                              : 48000)
                       : 48000;
        audio_clock_ts_ = std::make_unique<AudioClockTimeSource>(rate);
        time_source_ = audio_clock_ts_.get();
    }

    controller_.Opened(nullptr);
    compositor_.SetTimeSource(time_source_);
    XER_MEDIA_ILOG("player", "opened " + path);
    return true;
}

void MediaPlayerImpl::Close() {
    Stop();
    audio_stream_.reset();
    video_stream_.reset();
    if (demuxer_) demuxer_->Close();
}

void MediaPlayerImpl::Play() {
    if (!controller_.Play(nullptr)) return;
    if (audio_stream_)
        audio_stream_->Start(
            [this](DecodedFrame f) { OnAudioDecoded(std::move(f)); });
    if (video_stream_)
        video_stream_->Start(
            [this](DecodedFrame f) { OnVideoDecoded(std::move(f)); });
    time_source_->SetPlaybackRate(1.0);
    if (audio_clock_ts_) audio_clock_ts_->StartMediaTime(0);
    else if (wall_ts_) wall_ts_->StartMediaTime(0);
    if (audio_sink_) {
        audio_sink_->Start([this] {
            // Sink asks for a block — the clock advances by what was
            // consumed; renderer delivery fills it via the device path.
        });
        audio_sink_->Play();
    }
    if (video_sink_)
        video_sink_->Start([this] { PumpWatchTime(); });
    last_watch_mark_ = media_time();
}

void MediaPlayerImpl::Pause() {
    if (!controller_.Pause(nullptr)) return;
    if (audio_sink_) audio_sink_->Pause();
    if (audio_clock_ts_) audio_clock_ts_->StopClock();
    else if (wall_ts_) wall_ts_->StopClock();
    PumpWatchTime();
}

void MediaPlayerImpl::Stop() {
    PumpWatchTime();
    if (audio_stream_) audio_stream_->Stop();
    if (video_stream_) video_stream_->Stop();
    if (audio_sink_) audio_sink_->Stop();
    if (video_sink_) video_sink_->Stop();
    controller_.Stop(nullptr);
}

bool MediaPlayerImpl::Seek(TimeTicks position, std::string* error_out) {
    if (!demuxer_ || !demuxer_->IsOpen()) {
        if (error_out) *error_out = "no open media";
        return false;
    }
    if (audio_stream_) audio_stream_->Reset();
    if (video_stream_) video_stream_->Reset();
    compositor_.Flush();
    const bool ok = demuxer_->Seek(position, error_out);
    if (ok) {
        if (audio_clock_ts_) {
            audio_clock_ts_->clock()->Reset();
            audio_clock_ts_->StartMediaTime(position);
        } else if (wall_ts_) {
            wall_ts_->StartMediaTime(position);
        }
    }
    return ok;
}

void MediaPlayerImpl::SetVolume(double volume) {
    if (audio_sink_) audio_sink_->SetVolume(volume);
}

void MediaPlayerImpl::OnAudioDecoded(DecodedFrame frame) {
    ++stats_.audio_frames_decoded;
    stats_.audio_bytes_decoded += frame.pixels.size();
    // The clock advances by consumed frames once the sink pulls them;
    // until then buffer bookkeeping stays with the renderer.
    if (audio_clock_ts_ && !frame.pixels.empty())
        stats_.audio_frames_rendered++;
}

void MediaPlayerImpl::OnVideoDecoded(DecodedFrame frame) {
    ++stats_.video_frames_decoded;
    stats_.video_bytes_decoded += frame.pixels.size();
    compositor_.AddFrame(std::move(frame));
    ++stats_.video_frames_rendered;
    PumpWatchTime();
}

void MediaPlayerImpl::PumpWatchTime() {
    const TimeTicks now = media_time();
    if (last_watch_mark_ != kNoTimestamp && now > last_watch_mark_) {
        watch_time_.RecordWatchTime(WatchTimeKey::kAll,
                                    now - last_watch_mark_);
    }
    last_watch_mark_ = now;
}

TimeTicks MediaPlayerImpl::media_time() const {
    return time_source_ ? time_source_->CurrentMediaTime() : 0;
}

TimeTicks MediaPlayerImpl::duration() const {
    return demuxer_ ? demuxer_->duration() : 0;
}

const DecodedFrame* MediaPlayerImpl::CurrentVideoFrame() {
    return compositor_.CurrentFrame();
}

}  // namespace XER::Media
