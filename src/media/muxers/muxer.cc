#include "media/muxers/muxer.h"

#include "media/muxers/ffmpeg_muxer.h"

namespace XER::Media {

std::unique_ptr<Muxer> CreateMuxer(Container container) {
    // One FFmpeg-backed implementation covers all three containers; the
    // codec gate is enforced by ContainerSupportsCodec.
    return std::make_unique<FFmpegMuxer>(container);
}

}  // namespace XER::Media
