/**
 * FFmpegDemuxer — XER::Media::Demuxer backed by the existing FFmpeg bridge
 * (src/wrapper/ffmpeg, XER::Bridge::FFmpeg::VideoDecoder). Used for normal
 * src= playback where MediaPlayerImpl loads bytes itself.
 */
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "media/demuxer/demuxer.h"

namespace XER::Bridge::FFmpeg { class VideoDecoder; }

namespace XER::Media {

class FFmpegDemuxer final : public Demuxer {
public:
    FFmpegDemuxer();
    ~FFmpegDemuxer() override;

    bool Open(const std::string& path, std::string* error_out) override;
    void Close() override;
    bool IsOpen() const override;
    const std::vector<DemuxerStream*>& streams() const override;
    bool Seek(TimeTicks position, std::string* error_out) override;
    TimeTicks duration() const override;

    // Access to the underlying bridge for embedders that need the
    // extended API (audio tap pacing, epochs).
    XER::Bridge::FFmpeg::VideoDecoder* bridge() const { return decoder_.get(); }

private:
    std::unique_ptr<XER::Bridge::FFmpeg::VideoDecoder> decoder_;
    std::vector<std::unique_ptr<DemuxerStream>> owned_streams_;
    std::vector<DemuxerStream*> streams_;
    bool audio_enabled_ = false;
};

}  // namespace XER::Media
