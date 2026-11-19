#include "media/decoders/decoder_stream.h"

#include <utility>

namespace XER::Media {

DecoderStream::DecoderStream(DemuxerStream* stream, Decoder* decoder)
    : stream_(stream), decoder_(decoder) {}

bool DecoderStream::Initialize(std::string* error_out) {
    if (!stream_ || !decoder_) {
        if (error_out) *error_out = "decoder stream: null stream or decoder";
        return false;
    }
    if (!decoder_->CanDecode(stream_->codec())) {
        if (error_out) *error_out = "decoder does not support codec";
        return false;
    }
    return decoder_->Initialize(*stream_, error_out);
}

void DecoderStream::Start(FrameCallback cb) {
    frame_cb_ = std::move(cb);
    running_ = true;
    eos_ = false;
    IssueRead();
}

void DecoderStream::Stop() {
    running_ = false;
}

void DecoderStream::Reset() {
    read_pending_ = false;
    eos_ = false;
    if (decoder_) decoder_->Reset();
}

void DecoderStream::IssueRead() {
    if (!running_ || read_pending_ || eos_) return;
    read_pending_ = true;
    stream_->Read([this](DemuxerStream::ReadResult r) { OnRead(std::move(r)); });
}

void DecoderStream::OnRead(DemuxerStream::ReadResult result) {
    read_pending_ = false;
    if (!running_) return;
    if (result.end_of_stream) {
        eos_ = true;
        return;
    }
    if (!result.ok) {
        IssueRead();
        return;
    }
    decoder_->Decode(result,
                     [this](PipelineStatus s, DecodedFrame f) { OnDecoded(s, std::move(f)); });
}

void DecoderStream::OnDecoded(PipelineStatus status, DecodedFrame frame) {
    if (status == PipelineStatus::kOk && frame_cb_) frame_cb_(std::move(frame));
    IssueRead();
}

}  // namespace XER::Media
