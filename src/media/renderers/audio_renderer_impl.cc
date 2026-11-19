#include "media/renderers/audio_renderer_impl.h"

#include <utility>

namespace XER::Media {

AudioRendererImpl::AudioRendererImpl(std::unique_ptr<Decoder> decoder)
    : decoder_(std::move(decoder)) {}

bool AudioRendererImpl::Initialize(Demuxer* demuxer, AudioRendererSink* sink,
                                   std::string* error_out) {
    if (!demuxer) {
        if (error_out) *error_out = "audio renderer: null demuxer";
        return false;
    }
    sink_ = sink;
    auto* s = demuxer->GetStream(StreamType::kAudio);
    if (!s) return true;  // video-only media is legal
    if (!decoder_ || !decoder_->CanDecode(s->codec())) {
        if (error_out) *error_out = "no audio decoder for codec";
        return false;
    }
    stream_ = std::make_unique<DecoderStream>(s, decoder_.get());
    if (!stream_->Initialize(error_out)) {
        stream_.reset();
        return false;
    }
    if (sink_) {
        sink_->Start([this] {
            // Sink pull: decoded frames queue up for the platform stream.
        });
    }
    return true;
}

void AudioRendererImpl::Play() {
    playing_ = true;
    clock_.StartMediaTime(clock_.CurrentMediaTime());
    if (stream_) {
        stream_->Start([this](DecodedFrame f) {
            std::lock_guard lock(mu_);
            queue_.push_back(std::move(f));
        });
    }
    if (sink_) sink_->Play();
}

void AudioRendererImpl::Pause() {
    playing_ = false;
    clock_.StopClock();
    if (stream_) stream_->Stop();
    if (sink_) sink_->Pause();
}

void AudioRendererImpl::Flush() {
    if (stream_) stream_->Reset();
    std::lock_guard lock(mu_);
    queue_.clear();
}

bool AudioRendererImpl::PopFrame(DecodedFrame* out) {
    std::lock_guard lock(mu_);
    if (queue_.empty()) return false;
    *out = std::move(queue_.front());
    queue_.pop_front();
    return true;
}

int AudioRendererImpl::RenderInto(AudioBus* dst, int frames) {
    if (!dst || frames <= 0) return 0;
    std::lock_guard lock(mu_);
    // Drain whole decoded frames into the fifo (decoded payload is
    // interleaved f32 PCM in .pixels).
    while (!queue_.empty() && fifo_.frames() < fifo_.max_frames() / 2) {
        DecodedFrame& f = queue_.front();
        const int in_frames = f.width > 0 ? f.width
                                          : static_cast<int>(
                                                f.pixels.size() /
                                                (fifo_.channels() *
                                                 sizeof(float)));
        if (in_frames <= 0) {
            queue_.pop_front();
            continue;
        }
        auto bus = AudioBus::Create(fifo_.channels(), in_frames);
        bus.FromInterleavedFormat(SampleFormat::kF32, f.pixels.data(),
                                  in_frames);
        if (fifo_.Push(bus, in_frames) <= 0) break;
        queue_.pop_front();
    }
    return fifo_.Pop(dst, frames);
}

}  // namespace XER::Media
