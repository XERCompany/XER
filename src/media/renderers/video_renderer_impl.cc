#include "media/renderers/video_renderer_impl.h"

#include <utility>

namespace XER::Media {

VideoRendererImpl::VideoRendererImpl(std::unique_ptr<Decoder> decoder)
    : decoder_(std::move(decoder)) {}

bool VideoRendererImpl::Initialize(Demuxer* demuxer, VideoRendererSink* sink,
                                   TimeSource* time_source,
                                   std::string* error_out) {
    if (!demuxer) {
        if (error_out) *error_out = "video renderer: null demuxer";
        return false;
    }
    sink_ = sink;
    compositor_.SetTimeSource(time_source);
    auto* s = demuxer->GetStream(StreamType::kVideo);
    if (!s) {
        if (error_out) *error_out = "no video stream";
        return false;
    }
    if (!decoder_ || !decoder_->CanDecode(s->codec())) {
        if (error_out) *error_out = "no video decoder for codec";
        return false;
    }
    stream_ = std::make_unique<DecoderStream>(s, decoder_.get());
    if (!stream_->Initialize(error_out)) {
        stream_.reset();
        return false;
    }
    if (sink_) sink_->Start([]{});
    return true;
}

void VideoRendererImpl::Play() {
    playing_ = true;
    if (stream_) {
        stream_->Start([this](DecodedFrame f) {
            compositor_.AddFrame(std::move(f));
        });
    }
}

void VideoRendererImpl::Pause() {
    playing_ = false;
    if (stream_) stream_->Stop();
}

void VideoRendererImpl::Flush() {
    if (stream_) stream_->Reset();
    compositor_.Flush();
}

}  // namespace XER::Media
