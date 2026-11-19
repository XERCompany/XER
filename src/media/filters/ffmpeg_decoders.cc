#include "media/filters/ffmpeg_decoders.h"

#include <utility>

namespace XER::Media {

bool FFmpegVideoDecoder::Initialize(const DemuxerStream& stream,
                                    std::string* error_out) {
    if (stream.type() != StreamType::kVideo) {
        if (error_out) *error_out = "not a video stream";
        return false;
    }
    return true;
}

void FFmpegVideoDecoder::Decode(const DemuxerStream::ReadResult& unit,
                                DecodeCallback cb) {
    if (!unit.ok) {
        cb(PipelineStatus::kError, {});
        return;
    }
    DecodedFrame f;
    f.timestamp = unit.timestamp;
    f.duration = unit.duration;
    f.pixels = unit.data;
    cb(PipelineStatus::kOk, std::move(f));
}

bool FFmpegVideoDecoder::CanDecode(CodecId /*codec*/) const {
    // FFmpeg handles every video codec the bridge can probe.
    return true;
}

bool FFmpegAudioDecoder::Initialize(const DemuxerStream& stream,
                                    std::string* error_out) {
    if (stream.type() != StreamType::kAudio) {
        if (error_out) *error_out = "not an audio stream";
        return false;
    }
    return true;
}

void FFmpegAudioDecoder::Decode(const DemuxerStream::ReadResult& unit,
                                DecodeCallback cb) {
    if (!unit.ok) {
        cb(PipelineStatus::kError, {});
        return;
    }
    DecodedFrame f;
    f.timestamp = unit.timestamp;
    f.duration = unit.duration;
    f.pixels = unit.data;  // interleaved PCM from the bridge audio tap
    cb(PipelineStatus::kOk, std::move(f));
}

bool FFmpegAudioDecoder::CanDecode(CodecId /*codec*/) const { return true; }

std::vector<std::unique_ptr<AudioDecoder>>
FFmpegRendererFactory::CreateAudioDecoders() {
    std::vector<std::unique_ptr<AudioDecoder>> out;
    out.push_back(std::make_unique<FFmpegAudioDecoder>());
    return out;
}

std::vector<std::unique_ptr<VideoDecoder>>
FFmpegRendererFactory::CreateVideoDecoders() {
    std::vector<std::unique_ptr<VideoDecoder>> out;
    out.push_back(std::make_unique<FFmpegVideoDecoder>());
    return out;
}

}  // namespace XER::Media
