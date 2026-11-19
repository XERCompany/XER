#include "media/renderers/renderer_impl.h"

#include <utility>

namespace XER::Media {

RendererImpl::RendererImpl(std::unique_ptr<Decoder> audio_decoder,
                           std::unique_ptr<Decoder> video_decoder)
    : audio_(std::make_unique<AudioRendererImpl>(std::move(audio_decoder))),
      video_(std::make_unique<VideoRendererImpl>(std::move(video_decoder))) {}

bool RendererImpl::Initialize(Demuxer* demuxer,
                              AudioRendererSink* audio_sink,
                              VideoRendererSink* video_sink,
                              std::string* error_out) {
    demuxer_ = demuxer;
    audio_sink_ = audio_sink;
    // Audio first — it owns the master TimeSource the video syncs to.
    if (!audio_->Initialize(demuxer, audio_sink, error_out)) return false;
    if (!video_->Initialize(demuxer, video_sink,
                            audio_->time_source(), error_out)) {
        return false;
    }
    return true;
}

void RendererImpl::Play() {
    audio_->Play();
    video_->Play();
}

void RendererImpl::Pause() {
    audio_->Pause();
    video_->Pause();
}

void RendererImpl::Stop() {
    audio_->Pause();
    video_->Pause();
    audio_->Flush();
    video_->Flush();
}

bool RendererImpl::Seek(TimeTicks position, std::string* error_out) {
    if (!demuxer_) {
        if (error_out) *error_out = "no demuxer";
        return false;
    }
    audio_->Flush();
    video_->Flush();
    if (!demuxer_->Seek(position, error_out)) return false;
    return true;
}

void RendererImpl::SetVolume(double volume) {
    if (audio_sink_) audio_sink_->SetVolume(volume);
}

TimeTicks RendererImpl::media_time() const {
    auto* ts = audio_->time_source();
    return ts ? ts->CurrentMediaTime() : 0;
}

}  // namespace XER::Media
