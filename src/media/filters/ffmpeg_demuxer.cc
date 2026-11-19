#include "media/filters/ffmpeg_demuxer.h"

#include <utility>

#include "wrapper/ffmpeg/ffmpeg_video_decoder.h"
#include "wrapper/ffmpeg/ffmpeg_engine_bridge.h"

namespace XER::Media {
namespace {

// Bridge decodes internally, so DemuxerStream::Read() delivers decoded
// units (BGRA for video, PCM for audio) synchronously via the callback.
class FFmpegVideoStream final : public DemuxerStream {
public:
    explicit FFmpegVideoStream(XER::Bridge::FFmpeg::VideoDecoder* d)
        : decoder_(d) {}

    StreamType type() const override { return StreamType::kVideo; }
    CodecId codec() const override { return CodecId::kUnknown; }

    void Read(ReadCallback cb) override {
        ReadResult r;
        int w = 0, h = 0;
        if (decoder_->NextFrame(r.data, w, h)) {
            r.ok = true;
            const double pos = decoder_->GetPositionSeconds();
            r.timestamp = pos >= 0.0
                              ? static_cast<TimeTicks>(pos * 1'000'000.0)
                              : kNoTimestamp;
        } else {
            r.end_of_stream = true;
        }
        cb(std::move(r));
    }

private:
    XER::Bridge::FFmpeg::VideoDecoder* decoder_;
};

class FFmpegAudioStream final : public DemuxerStream {
public:
    explicit FFmpegAudioStream(XER::Bridge::FFmpeg::VideoDecoder* d)
        : decoder_(d) {}

    StreamType type() const override { return StreamType::kAudio; }
    CodecId codec() const override { return CodecId::kUnknown; }

    void Read(ReadCallback cb) override {
        ReadResult r;
        std::vector<XER::Bridge::FFmpeg::AudioFrameInfo> frames;
        if (decoder_->ReadAudioFrames(&frames, 8) && !frames.empty()) {
            const auto& f = frames.front();
            r.ok = true;
            r.data = f.data;
            const double tb = decoder_->GetAudioTimeBase();
            r.timestamp = tb > 0.0
                              ? static_cast<TimeTicks>(f.pts * tb * 1'000'000.0)
                              : kNoTimestamp;
        } else if (decoder_->AudioDrained()) {
            r.end_of_stream = true;
        }
        cb(std::move(r));
    }

private:
    XER::Bridge::FFmpeg::VideoDecoder* decoder_;
};

}  // namespace

FFmpegDemuxer::FFmpegDemuxer()
    : decoder_(std::make_unique<XER::Bridge::FFmpeg::VideoDecoder>()) {}

FFmpegDemuxer::~FFmpegDemuxer() = default;

bool FFmpegDemuxer::Open(const std::string& path, std::string* error_out) {
    decoder_->EnableAudioTap(true);
    if (!decoder_->Open(path, error_out)) return false;
    owned_streams_.push_back(std::make_unique<FFmpegVideoStream>(decoder_.get()));
    if (decoder_->HasAudio()) {
        audio_enabled_ = true;
        owned_streams_.push_back(
            std::make_unique<FFmpegAudioStream>(decoder_.get()));
    }
    streams_.clear();
    for (auto& s : owned_streams_) streams_.push_back(s.get());
    return true;
}

void FFmpegDemuxer::Close() {
    streams_.clear();
    owned_streams_.clear();
    decoder_->Close();
}

bool FFmpegDemuxer::IsOpen() const { return decoder_->IsOpen(); }

const std::vector<DemuxerStream*>& FFmpegDemuxer::streams() const {
    return streams_;
}

bool FFmpegDemuxer::Seek(TimeTicks position, std::string* error_out) {
    if (!decoder_->IsOpen()) {
        if (error_out) *error_out = "demuxer not open";
        return false;
    }
    if (!decoder_->Seek(static_cast<double>(position) / 1'000'000.0)) {
        if (error_out) *error_out = "seek failed";
        return false;
    }
    return true;
}

TimeTicks FFmpegDemuxer::duration() const {
    return static_cast<TimeTicks>(decoder_->GetDurationSeconds() * 1'000'000.0);
}

}  // namespace XER::Media
